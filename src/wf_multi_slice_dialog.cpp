// WeatherFiles area-first multi-slice download dialog. See header.

#include "wf_multi_slice_dialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/datetime.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>  // wxBusyCursor

#include <algorithm>
#include <unordered_set>

#include "weatherfiles_pi.h"
#include "wf_download_progress.h"

enum {
  ID_WF_MS_USEVIEW = wxID_HIGHEST + 60,
  ID_WF_MS_DRAW,
  ID_WF_MS_DOWNLOAD,
  ID_WF_MS_CHECK_BASE,  // checkbox ids derive from this + row index
};

static const int ATMO_CAP = 3;
static const int WAVE_CAP = 1;
static const int CURR_CAP = 1;

static wxString CategorizeModel(const WfModel& m) {
  bool has_wave = false, has_current = false;
  for (const auto& p : m.params) {
    if (p == "waves" || p == "swell" || p == "wind-waves") has_wave = true;
    if (p == "currents") has_current = true;
  }
  if (has_wave) return "wave";
  if (has_current) return "current";
  // Synoptic and other image-format products are filtered out at populate
  // time, not categorised here.
  return "atmosphere";
}

static wxString FormatModelLabel(const WfModel& m) {
  wxString params;
  for (size_t i = 0; i < m.params.size() && i < 4; ++i) {
    if (i) params += ", ";
    params += m.params[i];
  }
  if (m.params.size() > 4)
    params += wxString::Format(", +%zu", m.params.size() - 4);
  return wxString::Format("%s  -  %dh, [%s]",
                          m.name.IsEmpty() ? m.id : m.name,
                          m.forecast_horizon_h, params);
}

static wxString FmtDeg(double v) { return wxString::Format("%.4f", v); }

WfMultiSliceDialog::WfMultiSliceDialog(wxWindow* parent, const wxString& token,
                                       const WfBBox& default_box,
                                       weatherfiles_pi* owner)
    : wxDialog(parent, wxID_ANY, _("Download multiple slices for an area"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_token(token),
      m_api(token),
      m_owner(owner),
      m_default_box(default_box) {
  auto* top = new wxBoxSizer(wxVERTICAL);

  // Area row.
  auto* area_box = new wxStaticBoxSizer(wxVERTICAL, this, _("Area (degrees)"));
  auto* grid = new wxFlexGridSizer(2, 4, 8, 8);
  double n0 = default_box.valid ? default_box.north : 50.0;
  double s0 = default_box.valid ? default_box.south : 40.0;
  double w0 = default_box.valid ? default_box.west : -5.0;
  double e0 = default_box.valid ? default_box.east : 10.0;
  m_north = new wxTextCtrl(this, wxID_ANY, FmtDeg(n0));
  m_south = new wxTextCtrl(this, wxID_ANY, FmtDeg(s0));
  m_west = new wxTextCtrl(this, wxID_ANY, FmtDeg(w0));
  m_east = new wxTextCtrl(this, wxID_ANY, FmtDeg(e0));
  grid->Add(new wxStaticText(this, wxID_ANY, _("North")), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_north, 1, wxEXPAND);
  grid->Add(new wxStaticText(this, wxID_ANY, _("West")), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_west, 1, wxEXPAND);
  grid->Add(new wxStaticText(this, wxID_ANY, _("South")), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_south, 1, wxEXPAND);
  grid->Add(new wxStaticText(this, wxID_ANY, _("East")), 0,
            wxALIGN_CENTER_VERTICAL);
  grid->Add(m_east, 1, wxEXPAND);
  grid->AddGrowableCol(1, 1);
  grid->AddGrowableCol(3, 1);
  area_box->Add(grid, 0, wxEXPAND | wxALL, 6);
  auto* area_row = new wxBoxSizer(wxHORIZONTAL);
  area_row->Add(new wxButton(this, ID_WF_MS_USEVIEW, _("Use chart view")), 0,
                wxRIGHT, 8);
  area_row->Add(new wxButton(this, ID_WF_MS_DRAW, _("Draw on chart")), 0);
  area_box->Add(area_row, 0, wxLEFT | wxBOTTOM, 6);
  top->Add(area_box, 0, wxEXPAND | wxALL, 8);

  // Scrolled body: 3 category sections will be added after Load().
  m_body = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                wxSize(560, 320), wxVSCROLL);
  m_body->SetScrollRate(0, 12);
  auto* body_sz = new wxBoxSizer(wxVERTICAL);
  m_body->SetSizer(body_sz);
  top->Add(m_body, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  // Forecast window/step row.
  auto* tr = new wxBoxSizer(wxHORIZONTAL);
  tr->Add(new wxStaticText(this, wxID_ANY, _("Forecast hours:")), 0,
          wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  m_window = new wxSpinCtrl(this, wxID_ANY, "48", wxDefaultPosition,
                            wxSize(80, -1), wxSP_ARROW_KEYS, 1, 384, 48);
  tr->Add(m_window, 0, wxRIGHT, 16);
  tr->Add(new wxStaticText(this, wxID_ANY, _("Step (h):")), 0,
          wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
  m_step = new wxSpinCtrl(this, wxID_ANY, "3", wxDefaultPosition,
                          wxSize(70, -1), wxSP_ARROW_KEYS, 1, 24, 3);
  tr->Add(m_step);
  top->Add(tr, 0, wxALL, 10);

  m_status = new wxStaticText(this, wxID_ANY, _("Loading models..."));
  top->Add(m_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);

  auto* btn_row = new wxBoxSizer(wxHORIZONTAL);
  m_download = new wxButton(this, ID_WF_MS_DOWNLOAD, _("Download slices"));
  m_download->Disable();
  btn_row->Add(m_download, 0, wxRIGHT, 8);
  btn_row->AddStretchSpacer();
  btn_row->Add(new wxButton(this, wxID_CANCEL, _("Close")), 0);
  top->Add(btn_row, 0, wxEXPAND | wxALL, 10);

  SetSizerAndFit(top);
  SetMinSize(wxSize(620, 600));

  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnUseView, this, ID_WF_MS_USEVIEW);
  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnDraw, this, ID_WF_MS_DRAW);
  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnDownload, this, ID_WF_MS_DOWNLOAD);
  Bind(wxEVT_CLOSE_WINDOW, &WfMultiSliceDialog::OnClose, this);
  // wxID_CANCEL in a modeless dialog by default hides; route it through Close
  // so OnClose() can Destroy().
  Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, wxID_CANCEL);

  CallAfter([this]() { Load(); });
}

WfMultiSliceDialog::~WfMultiSliceDialog() {
  if (m_owner) m_owner->ClearMultiDialog(this);
}

void WfMultiSliceDialog::OnClose(wxCloseEvent&) {
  // Modeless: the default wxDialog handler just hides; we want it gone so the
  // plugin's pointer clears and the next toolbar click rebuilds it fresh.
  Destroy();
}

void WfMultiSliceDialog::SetBox(const WfBBox& box) {
  if (!box.valid) return;
  m_north->SetValue(FmtDeg(box.north));
  m_south->SetValue(FmtDeg(box.south));
  m_west->SetValue(FmtDeg(box.west));
  m_east->SetValue(FmtDeg(box.east));
  UpdateSelectionState();
  Raise();
}

void WfMultiSliceDialog::Load() {
  m_status->SetLabel(_("Loading models..."));
  wxBusyCursor busy;
  m_api.ValidateToken([this](bool ok, const WfAccount& acct,
                             const wxString& err) {
    if (!ok) {
      m_status->SetLabel(_("Token error: ") + err);
      return;
    }
    m_allowed = acct.allowed_models;
    m_api.FetchModels([this, acct](bool ok2, const std::vector<WfModel>& models,
                                   const wxString& err2) {
      if (!ok2) {
        m_status->SetLabel(_("Failed to load models: ") + err2);
        return;
      }
      m_rows.clear();
      m_rows.reserve(models.size());
      for (const auto& m : models) {
        // Filter to GRIB-only; image-format synoptic charts don't fit this
        // flow.
        if (m.format != "grib" && !m.format.IsEmpty()) continue;
        ModelRow r;
        r.model = m;
        r.category = CategorizeModel(m);
        m_rows.push_back(r);
      }
      Populate();
      m_status->SetLabel(wxString::Format(
          _("Signed in as %s - %s tier - pick up to 3 atmosphere + 1 wave + "
            "1 current."),
          acct.email, acct.tier));
    });
  });
}

void WfMultiSliceDialog::BuildModelSection(wxScrolledWindow* host,
                                           wxSizer* host_sizer,
                                           const wxString& title,
                                           const wxString& category, int cap) {
  auto* box = new wxStaticBoxSizer(
      wxVERTICAL, host,
      wxString::Format(_("%s (up to %d)"), title, cap));
  host_sizer->Add(box, 0, wxEXPAND | wxALL, 6);

  // Sort by name for stable display.
  std::vector<ModelRow*> rows_in_cat;
  for (auto& r : m_rows)
    if (r.category == category) rows_in_cat.push_back(&r);
  std::sort(rows_in_cat.begin(), rows_in_cat.end(),
            [](const ModelRow* a, const ModelRow* b) {
              return a->model.name.Cmp(b->model.name) < 0;
            });

  if (rows_in_cat.empty()) {
    auto* none = new wxStaticText(host, wxID_ANY, _("  (none)"));
    none->SetForegroundColour(wxColour(0x80, 0x80, 0x80));
    box->Add(none, 0, wxLEFT | wxTOP | wxBOTTOM, 4);
  }

  for (ModelRow* r : rows_in_cat) {
    wxString label = FormatModelLabel(r->model);
    const bool allowed =
        m_allowed.empty() ||
        std::find(m_allowed.begin(), m_allowed.end(), r->model.id) !=
            m_allowed.end();
    if (!allowed) label += _("  (Pro tier)");

    auto* cb = new wxCheckBox(host, wxID_ANY, label);
    if (!allowed) cb->Disable();
    cb->Bind(wxEVT_CHECKBOX,
             [this](wxCommandEvent& evt) { OnCheck(evt); });
    box->Add(cb, 0, wxLEFT | wxTOP, 2);
    r->check = cb;
  }

  // Remember the section sizer for later coverage refresh.
  if (category == "atmosphere") m_atmoSizer = box;
  else if (category == "wave") m_waveSizer = box;
  else if (category == "current") m_currSizer = box;
}

void WfMultiSliceDialog::Populate() {
  auto* body_sz = m_body->GetSizer();
  body_sz->Clear(true);

  BuildModelSection(m_body, body_sz, _("Atmosphere"), "atmosphere", ATMO_CAP);
  BuildModelSection(m_body, body_sz, _("Waves"), "wave", WAVE_CAP);
  BuildModelSection(m_body, body_sz, _("Currents"), "current", CURR_CAP);

  m_body->FitInside();
  m_body->Layout();
  UpdateSelectionState();
  Layout();
}

bool WfMultiSliceDialog::BBoxIntersects(const WfModel& m, double s, double n,
                                        double w, double e) const {
  // Simple lat/lon overlap; longitudes are not wrapped (good enough for the
  // European/Atlantic models we ship).
  if (n <= m.south || s >= m.north) return false;
  if (e <= m.west || w >= m.east) return false;
  return true;
}

void WfMultiSliceDialog::OnCheck(wxCommandEvent& evt) {
  // Enforce caps: if the user just *checked* a row that puts the category
  // over its cap, uncheck it again and flash the status.
  auto* src = wxDynamicCast(evt.GetEventObject(), wxCheckBox);
  if (!src) return;
  if (!src->IsChecked()) {
    UpdateSelectionState();
    return;
  }
  // Find this row's category.
  wxString cat;
  for (const auto& r : m_rows) {
    if (r.check == src) {
      cat = r.category;
      break;
    }
  }
  int cap = (cat == "atmosphere") ? ATMO_CAP
            : (cat == "wave")     ? WAVE_CAP
            : (cat == "current")  ? CURR_CAP
                                  : 0;
  int count = 0;
  for (const auto& r : m_rows)
    if (r.category == cat && r.check && r.check->IsChecked()) ++count;
  if (count > cap) {
    src->SetValue(false);
    m_status->SetLabel(wxString::Format(
        _("Maximum %d %s model(s) - uncheck one first."), cap, cat));
    return;
  }
  UpdateSelectionState();
}

void WfMultiSliceDialog::UpdateSelectionState() {
  // Re-apply "outside coverage" greying based on current bbox.
  const double n = FieldVal(m_north, 90);
  const double s = FieldVal(m_south, -90);
  const double w = FieldVal(m_west, -180);
  const double e = FieldVal(m_east, 180);
  const bool bbox_ok = (n > s) && (e > w);

  int total = 0;
  int min_horizon = INT32_MAX;
  int max_step = 1;
  for (auto& r : m_rows) {
    if (!r.check) continue;
    const bool in = !bbox_ok || BBoxIntersects(r.model, s, n, w, e);
    const bool allowed =
        m_allowed.empty() ||
        std::find(m_allowed.begin(), m_allowed.end(), r.model.id) !=
            m_allowed.end();
    if (!in && r.check->IsChecked()) r.check->SetValue(false);
    r.check->Enable(allowed && in);
    if (r.check->IsChecked()) {
      ++total;
      if (r.model.forecast_horizon_h > 0 &&
          r.model.forecast_horizon_h < min_horizon)
        min_horizon = r.model.forecast_horizon_h;
      const int step =
          r.model.step_h >= 1 ? static_cast<int>(r.model.step_h) : 1;
      if (step > max_step) max_step = step;
    }
  }
  if (total > 0 && min_horizon < INT32_MAX) {
    m_window->SetRange(1, min_horizon);
    if (m_window->GetValue() > min_horizon) m_window->SetValue(min_horizon);
    if (m_step->GetValue() < max_step) m_step->SetValue(max_step);
  }
  m_download->Enable(total > 0 && bbox_ok);
  if (total > 0) {
    m_download->SetLabel(
        wxString::Format(_("Download %d slices"), total));
  } else {
    m_download->SetLabel(_("Download slices"));
  }
  if (!bbox_ok) {
    m_status->SetLabel(_("Set North > South and East > West."));
  } else if (total == 0) {
    m_status->SetLabel(
        _("Pick up to 3 atmosphere + 1 wave + 1 current model."));
  } else {
    m_status->SetLabel(
        wxString::Format(_("%d model(s) selected for the area."), total));
  }
}

void WfMultiSliceDialog::OnUseView(wxCommandEvent&) {
  if (!m_default_box.valid) {
    m_status->SetLabel(_("No chart view available."));
    return;
  }
  SetBox(m_default_box);
}

void WfMultiSliceDialog::OnDraw(wxCommandEvent&) {
  if (!m_owner) return;
  m_status->SetLabel(_("Drag on the chart to draw an area..."));
  // Route through the plugin's MultiDialog() canary so a dialog destroyed
  // mid-pick (between LeftUp's m_pick_cb copy and CallAfter dispatch) becomes
  // a safe no-op rather than a dangling-pointer crash.
  weatherfiles_pi* owner = m_owner;
  owner->StartAreaPickMulti([owner](const WfBBox& box) {
    if (auto* dlg = owner->MultiDialog()) dlg->SetBox(box);
  });
}

double WfMultiSliceDialog::FieldVal(wxTextCtrl* c, double fallback) const {
  double v;
  if (c && c->GetValue().ToDouble(&v)) return v;
  return fallback;
}

void WfMultiSliceDialog::OnDownload(wxCommandEvent&) {
  const double n = FieldVal(m_north, 0);
  const double s = FieldVal(m_south, 0);
  const double w = FieldVal(m_west, 0);
  const double e = FieldVal(m_east, 0);
  if (n <= s || e <= w) {
    m_status->SetLabel(_("Set North > South and East > West."));
    return;
  }

  // Per-batch output dir under ~/Documents/WeatherFiles/<timestamp>/.
  const wxString ts = wxDateTime::Now().Format("%Y-%m-%dT%H%M");
  wxString base = wxFileName(wxStandardPaths::Get().GetDocumentsDir(), "")
                      .GetPathWithSep() +
                  "WeatherFiles";
  if (!wxDirExists(base))
    wxFileName::Mkdir(base, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  wxString dir = wxFileName(base, ts).GetFullPath();
  if (!wxDirExists(dir))
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  std::vector<WfDownloadJob> jobs;
  // Build jobs in send-friendly order: currents, waves, atmospheres - matches
  // the GRIB handoff order so the first atmosphere ends up the active file
  // whether grib_pi stacks or replaces.
  for (int phase = 0; phase < 3; ++phase) {
    const wxString want_cat =
        (phase == 0) ? "current" : (phase == 1) ? "wave" : "atmosphere";
    for (auto& r : m_rows) {
      if (!r.check || !r.check->IsChecked()) continue;
      if (r.category != want_cat) continue;
      WfDownloadJob j;
      j.label = r.model.name.IsEmpty() ? r.model.id : r.model.name;
      j.label += " (" + r.category + ")";
      j.category = r.category;

      // Clip the requested bbox to the model's coverage.
      const double cs = std::max(r.model.south, s);
      const double cn = std::min(r.model.north, n);
      const double cw = std::max(r.model.west, w);
      const double ce = std::min(r.model.east, e);

      wxString params;
      for (const auto& p : r.model.params) {
        if (!params.IsEmpty()) params += ",";
        params += p;
      }
      const wxString bbox =
          wxString::Format("%.4f,%.4f,%.4f,%.4f", cw, ce, cs, cn);
      j.query = wxString::Format(
          "/grib?model=%s&params=%s&bbox=%s&time_window_h=%d&time_step_h=%d",
          r.model.id, params, bbox, m_window->GetValue(), m_step->GetValue());
      j.out_path = wxFileName(dir, r.model.id + ".grib2").GetFullPath();
      jobs.push_back(j);
    }
  }
  if (jobs.empty()) {
    m_status->SetLabel(_("Pick at least one model."));
    return;
  }

  m_download->Disable();
  {
    WfDownloadProgress dlg(this, m_token, std::move(jobs));
    dlg.ShowModal();
  }
  m_download->Enable();
  m_status->SetLabel(_("Done. Pick another batch or close this dialog."));
}
