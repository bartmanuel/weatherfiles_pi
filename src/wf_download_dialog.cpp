// SPDX-FileCopyrightText: 2026 Bart Manuel
// SPDX-License-Identifier: GPL-3.0-or-later

// WeatherFiles one-shot GRIB download dialog. See wf_download_dialog.h.

#include "wf_download_dialog.h"

#include <algorithm>
#include <cstdint>

#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/datetime.h>
#include <wx/filefn.h>  // wxDirExists, wxFileName helpers
#include <wx/filename.h>
#include <wx/jsonval.h>
#include <wx/jsonwriter.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>  // wxBusyCursor


#include "ocpn_plugin.h"  // SendPluginMessage
#include "wf_download_progress.h"

enum { ID_WF_DOWNLOAD = wxID_HIGHEST + 20 };

static wxString fmtd(double v) { return wxString::Format("%.4f", v); }

WfDownloadDialog::WfDownloadDialog(wxWindow* parent, const WfModel& model,
                                   const WfBBox& def, const wxString& token)
    : wxDialog(parent, wxID_ANY,
               wxString::Format(_("Download - %s"),
                                model.name.IsEmpty() ? model.id : model.name),
               wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
      m_model(model),
      m_api(token) {
  // Default area = current view clamped to the model domain, else full domain.
  double s = model.south, n = model.north, w = model.west, e = model.east;
  if (def.valid) {
    double cs = std::max(model.south, std::min(def.south, model.north));
    double cn = std::min(model.north, std::max(def.north, model.south));
    double cw = std::max(model.west, std::min(def.west, model.east));
    double ce = std::min(model.east, std::max(def.east, model.west));
    if (cn > cs && ce > cw) { s = cs; n = cn; w = cw; e = ce; }
  }

  auto* top = new wxBoxSizer(wxVERTICAL);

  top->Add(new wxStaticText(this, wxID_ANY, _("Area (degrees):")), 0,
           wxLEFT | wxTOP, 10);
  auto* grid = new wxFlexGridSizer(2, 6, 8);
  m_north = new wxTextCtrl(this, wxID_ANY, fmtd(n), wxDefaultPosition, wxSize(100, -1));
  m_south = new wxTextCtrl(this, wxID_ANY, fmtd(s), wxDefaultPosition, wxSize(100, -1));
  m_west  = new wxTextCtrl(this, wxID_ANY, fmtd(w), wxDefaultPosition, wxSize(100, -1));
  m_east  = new wxTextCtrl(this, wxID_ANY, fmtd(e), wxDefaultPosition, wxSize(100, -1));
  grid->Add(new wxStaticText(this, wxID_ANY, _("North")), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_north);
  grid->Add(new wxStaticText(this, wxID_ANY, _("South")), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_south);
  grid->Add(new wxStaticText(this, wxID_ANY, _("West")), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_west);
  grid->Add(new wxStaticText(this, wxID_ANY, _("East")), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_east);
  top->Add(grid, 0, wxALL, 10);

  top->Add(new wxStaticText(this, wxID_ANY, _("Parameters:")), 0, wxLEFT, 10);
  wxArrayString ps;
  for (const wxString& p : m_model.params) ps.Add(p);
  m_params = new wxCheckListBox(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 110), ps);
  for (unsigned i = 0; i < m_params->GetCount(); ++i) m_params->Check(i, true);
  top->Add(m_params, 0, wxEXPAND | wxALL, 10);

  auto* hs = new wxBoxSizer(wxHORIZONTAL);
  hs->Add(new wxStaticText(this, wxID_ANY, _("Forecast hours:")), 0,
          wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  int max_h = m_model.forecast_horizon_h > 0 ? m_model.forecast_horizon_h : 240;
  m_window = new wxSpinCtrl(this, wxID_ANY, wxString::Format("%d", max_h),
                            wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1,
                            max_h, max_h);
  hs->Add(m_window, 0, wxRIGHT, 16);
  hs->Add(new wxStaticText(this, wxID_ANY, _("Step (h):")), 0,
          wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  int def_step = m_model.step_h >= 1 ? (int)m_model.step_h : 3;
  m_step = new wxSpinCtrl(this, wxID_ANY, wxString::Format("%d", def_step),
                          wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 24,
                          def_step);
  hs->Add(m_step);
  top->Add(hs, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

  m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_status, 0, wxALL, 10);

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  m_download = new wxButton(this, ID_WF_DOWNLOAD, _("Download && open"));
  row->Add(m_download, 0, wxRIGHT, 8);
  row->AddStretchSpacer();
  row->Add(new wxButton(this, wxID_CANCEL, _("Close")), 0);
  top->Add(row, 0, wxEXPAND | wxALL, 10);

  SetSizerAndFit(top);
  Bind(wxEVT_BUTTON, &WfDownloadDialog::OnDownload, this, ID_WF_DOWNLOAD);
}

void WfDownloadDialog::SetStatus(const wxString& text, bool ok) {
  if (!m_status) return;
  m_status->SetForegroundColour(ok ? wxColour(0x1a, 0x9e, 0x1a)
                                   : wxColour(0xe0, 0x40, 0x40));
  m_status->SetLabel(text);
  Layout();
  m_status->Update();  // immediate repaint (HTTP calls block the GUI thread)
}

double WfDownloadDialog::FieldVal(wxTextCtrl* c, double fallback) const {
  double v;
  if (c && c->GetValue().ToDouble(&v)) return v;
  return fallback;
}

void WfDownloadDialog::OnDownload(wxCommandEvent&) {
  wxString params;
  for (unsigned i = 0; i < m_params->GetCount(); ++i) {
    if (m_params->IsChecked(i)) {
      if (!params.IsEmpty()) params += ",";
      params += m_params->GetString(i);
    }
  }
  if (params.IsEmpty()) {
    SetStatus(_("Select at least one parameter."), false);
    return;
  }
  const double n = FieldVal(m_north, m_model.north);
  const double s = FieldVal(m_south, m_model.south);
  const double w = FieldVal(m_west, m_model.west);
  const double e = FieldVal(m_east, m_model.east);
  if (n <= s || e <= w) {
    SetStatus(_("North must be > South and East > West."), false);
    return;
  }

  // /v1/grib expects bbox as west,east,south,north.
  const wxString bbox = wxString::Format("%.4f,%.4f,%.4f,%.4f", w, e, s, n);
  const wxString query = wxString::Format(
      "/grib?model=%s&params=%s&bbox=%s&time_window_h=%d&time_step_h=%d",
      m_model.id, params, bbox, m_window->GetValue(), m_step->GetValue());

  // Save into a WeatherFiles folder under the user's documents.
  wxString dir = wxFileName(wxStandardPaths::Get().GetDocumentsDir(), "")
                     .GetPathWithSep() + "WeatherFiles";
  if (!wxDirExists(dir))
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  const wxString ts = wxDateTime::Now().Format("%Y%m%d_%H%M");
  const wxString out =
      wxFileName(dir, m_model.id + "_" + ts + ".grib2").GetFullPath();

  m_download->Disable();
  {
    // Threaded download with a live progress dialog (prep timer -> KB counter).
    WfDownloadProgress dlg(this, m_api.Token(), query, out);
    const int r = dlg.ShowModal();
    m_download->Enable();
    if (r != wxID_OK) {
      SetStatus(dlg.Cancelled() ? _("Download cancelled.")
                                : _("Download failed: ") + dlg.Error(),
                false);
      return;
    }
  }
  // Hand the file to OpenCPN's GRIB plugin via GRIB_APPLY_JSON_CONFIG. That
  // message is silently ignored unless the GRIB plugin is enabled with its
  // control panel open, so make the outcome explicit rather than appearing to
  // do nothing.
  wxJSONValue cfg;
  cfg[_T("grib_file")] = out;
  wxJSONWriter writer(wxJSONWRITER_NONE);
  wxString body;
  writer.Write(cfg, body);
  SendPluginMessage(_T("GRIB_APPLY_JSON_CONFIG"), body);

  const wxString gribDir = GetPluginDataDir("grib_pi");
  if (gribDir.IsEmpty() || !wxDirExists(gribDir)) {
    SetStatus(_("Downloaded, but OpenCPN's GRIB plugin was not found - enable it "
                "to view the data.\nSaved: ") + out, true);
  } else {
    SetStatus(_("Downloaded and sent to the GRIB display. If nothing appears, "
                "enable the GRIB plugin and open its control panel.\nSaved: ") +
                  out,
              true);
  }
}
