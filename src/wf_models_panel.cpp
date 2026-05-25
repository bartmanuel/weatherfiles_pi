// WeatherFiles model browser. See wf_models_panel.h.
//
// NOTE (build): wxWidgets 3.2 GUI code; the async WfApi callbacks fire on the
// GUI thread during the modal loop, so they update the list directly.

#include "wf_models_panel.h"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>

#include "wf_download_dialog.h"
#include "weatherfiles_pi.h"

enum { ID_WF_REFRESH = wxID_HIGHEST + 10, ID_WF_DOWNLOAD_SEL, ID_WF_PICK };

WfModelsPanel::WfModelsPanel(wxWindow* parent, const wxString& token,
                             const WfBBox& default_box, weatherfiles_pi* owner)
    : wxDialog(parent, wxID_ANY, _("WeatherFiles - Models"), wxDefaultPosition,
               wxSize(660, 440), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_api(token),
      m_owner(owner),
      m_default_box(default_box) {
  auto* top = new wxBoxSizer(wxVERTICAL);

  m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_status, 0, wxALL, 8);

  m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  m_list->InsertColumn(0, _("Model"), wxLIST_FORMAT_LEFT, 170);
  m_list->InsertColumn(1, _("Source"), wxLIST_FORMAT_LEFT, 90);
  m_list->InsertColumn(2, _("Region S,W,N,E"), wxLIST_FORMAT_LEFT, 175);
  m_list->InsertColumn(3, _("Params"), wxLIST_FORMAT_LEFT, 120);
  m_list->InsertColumn(4, _("Horizon"), wxLIST_FORMAT_RIGHT, 70);
  top->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  m_refresh = new wxButton(this, ID_WF_REFRESH, _("Refresh"));
  m_pick = new wxButton(this, ID_WF_PICK, _("Pick area on chart"));
  m_download = new wxButton(this, ID_WF_DOWNLOAD_SEL, _("Download..."));
  row->Add(m_refresh, 0, wxRIGHT, 8);
  row->Add(m_pick, 0, wxRIGHT, 8);
  row->Add(m_download, 0, wxRIGHT, 8);
  row->AddStretchSpacer();
  row->Add(new wxButton(this, wxID_CANCEL, _("Close")), 0);
  top->Add(row, 0, wxEXPAND | wxALL, 8);

  SetSizer(top);
  Bind(wxEVT_BUTTON, &WfModelsPanel::OnRefresh, this, ID_WF_REFRESH);
  Bind(wxEVT_BUTTON, &WfModelsPanel::OnDownload, this, ID_WF_DOWNLOAD_SEL);
  Bind(wxEVT_BUTTON, &WfModelsPanel::OnPick, this, ID_WF_PICK);
  Bind(wxEVT_LIST_ITEM_ACTIVATED, &WfModelsPanel::OnActivate, this);

  Load();
}

const WfModel* WfModelsPanel::SelectedAllowedModel() {
  long sel = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
  if (sel < 0 || sel >= (long)m_models.size()) {
    SetStatus(_("Select a model first."), false);
    return nullptr;
  }
  const WfModel& m = m_models[sel];
  if (std::find(m_allowed.begin(), m_allowed.end(), m.id) == m_allowed.end()) {
    SetStatus(_("That model requires the Pro tier."), false);
    return nullptr;
  }
  return &m_models[sel];
}

void WfModelsPanel::OnActivate(wxListEvent&) { OpenDownloadForSelection(); }

void WfModelsPanel::OnDownload(wxCommandEvent&) { OpenDownloadForSelection(); }

void WfModelsPanel::OpenDownloadForSelection() {
  const WfModel* m = SelectedAllowedModel();
  if (!m) return;
  WfDownloadDialog dlg(this, *m, m_default_box, m_api.Token());
  dlg.ShowModal();
}

void WfModelsPanel::OnPick(wxCommandEvent&) {
  const WfModel* m = SelectedAllowedModel();
  if (!m) return;
  if (m_owner) m_owner->StartAreaPick(*m);
  // Close the browser so the chart is interactive for the drag.
  EndModal(wxID_CANCEL);
}

void WfModelsPanel::SetStatus(const wxString& text, bool ok) {
  if (!m_status) return;
  m_status->SetForegroundColour(ok ? wxColour(0x1a, 0x9e, 0x1a)
                                   : wxColour(0xe0, 0x40, 0x40));
  m_status->SetLabel(text);
  m_status->Refresh();
  Layout();
}

void WfModelsPanel::OnRefresh(wxCommandEvent&) { Load(); }

void WfModelsPanel::Load() {
  if (m_api.Token().IsEmpty()) {
    SetStatus(_("No API token set - open Preferences and paste your token."),
              false);
    return;
  }
  m_refresh->Disable();
  SetStatus(_("Loading account..."), true);

  // Single request in flight at a time, so chain: account (for tier gating),
  // then the model catalogue.
  m_api.ValidateToken(
      [this](bool ok, const WfAccount& acct, const wxString& err) {
        if (!ok) {
          m_refresh->Enable();
          SetStatus(_("Token check failed: ") + err, false);
          return;
        }
        m_allowed = acct.allowed_models;
        SetStatus(_("Loading models..."), true);
        m_api.FetchModels([this, acct](bool ok2,
                                       const std::vector<WfModel>& models,
                                       const wxString& err2) {
          m_refresh->Enable();
          if (!ok2) {
            SetStatus(_("Failed to load models: ") + err2, false);
            return;
          }
          Populate(models);
          SetStatus(wxString::Format("%d models  -  %s (%s)",
                                     (int)models.size(), acct.email, acct.tier),
                    true);
        });
      });
}

void WfModelsPanel::Populate(const std::vector<WfModel>& models) {
  m_models = models;  // row index -> model, for the download dialog
  m_list->DeleteAllItems();
  long row = 0;
  for (const WfModel& m : models) {
    const wxString name = m.name.IsEmpty() ? m.id : m.name;
    const bool allowed =
        std::find(m_allowed.begin(), m_allowed.end(), m.id) != m_allowed.end();

    long i = m_list->InsertItem(row, allowed ? name : name + _(" (Pro)"));
    m_list->SetItem(i, 1, m.source);
    m_list->SetItem(i, 2,
                    wxString::Format("%.1f,%.1f,%.1f,%.1f", m.south, m.west,
                                     m.north, m.east));
    wxString params;
    for (size_t k = 0; k < m.params.size(); ++k) {
      if (k) params += ",";
      params += m.params[k];
    }
    m_list->SetItem(i, 3, params);
    m_list->SetItem(i, 4, wxString::Format("%dh", m.forecast_horizon_h));
    if (!allowed) m_list->SetItemTextColour(i, wxColour(0x90, 0x90, 0x90));
    ++row;
  }
}
