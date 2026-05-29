// WeatherFiles area-first multi-slice download wizard. See header.

#include "wf_multi_slice_dialog.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/dcmemory.h>
#include <wx/display.h>
#include <wx/richtooltip.h>
#include <wx/statbmp.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/gbsizer.h>
#include <wx/panel.h>
#include <wx/radiobox.h>
#include <wx/scrolwin.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <set>

// Local M_PI - C++ standard doesn't guarantee it via <cmath>, MSVC needs
// _USE_MATH_DEFINES before include.
static constexpr double WF_PI = 3.14159265358979323846;

#include "weatherfiles_pi.h"
#include "wf_download_progress.h"

enum {
  ID_WF_MS_USEVIEW = wxID_HIGHEST + 60,
  ID_WF_MS_DRAW,
  ID_WF_MS_DOWNLOAD,
  ID_WF_MS_BACK,
  ID_WF_MS_NEXT,
  ID_WF_MS_ANIM_TIMER,
  ID_WF_MS_NEW_AREA,           // "Download something else" on start page
};

static const int ATMO_CAP = 5;
static const int WAVE_CAP = 2;
static const int CURR_CAP = 2;

static const wxColour COL_FULL(0x16, 0x88, 0x40);     // green
static const wxColour COL_PARTIAL(0xc7, 0x6a, 0x00);  // orange
static const wxColour COL_OUTSIDE(0x80, 0x80, 0x80);  // grey
static const wxColour COL_BRAND(0x00, 0x54, 0xd6);    // brand blue

// Render a small filled CIRCLE bitmap. We paint the bg of the bitmap with
// the parent's background colour so the square bitmap corners visually
// blend into the surrounding control, making the dot read as a circle. The
// previous alpha-blended path silently rendered transparent on macOS.
static wxBitmap MakeColorCircle(const wxColour& fg, const wxColour& bg,
                                int d = 14) {
  wxBitmap bmp(d, d);   // default depth - no alpha
  wxMemoryDC dc(bmp);
  dc.SetBackground(wxBrush(bg));
  dc.Clear();
  dc.SetBrush(wxBrush(fg));
  dc.SetPen(wxPen(fg.ChangeLightness(50), 1));
  dc.DrawCircle(d / 2, d / 2, d / 2 - 1);
  dc.SelectObject(wxNullBitmap);
  return bmp;
}

// Canonical param order + letter map, mirroring PARAM_ORDER in
// frontend/src/routes/models/overview/+page.svelte. The V slot folds
// waves / swell / wind-waves into one; U folds currents / current.
struct ParamSlot { const char* code; const char* keys[4]; };
static const ParamSlot PARAM_SLOTS[] = {
    {"W", {"wind",          nullptr, nullptr, nullptr}},
    {"G", {"gusts",         nullptr, nullptr, nullptr}},
    {"P", {"pressure",      nullptr, nullptr, nullptr}},
    {"C", {"clouds",        nullptr, nullptr, nullptr}},
    {"A", {"cape",          nullptr, nullptr, nullptr}},
    {"I", {"cin",           nullptr, nullptr, nullptr}},
    {"R", {"precipitation", nullptr, nullptr, nullptr}},
    {"T", {"temperature",   nullptr, nullptr, nullptr}},
    {"H", {"humidity",      nullptr, nullptr, nullptr}},
    {"V", {"waves", "swell", "wind-waves", nullptr}},
    {"U", {"currents", "current", nullptr, nullptr}},
    {"S", {"synoptic",      nullptr, nullptr, nullptr}},
};

static wxString CategorizeModel(const WfModel& m) {
  bool has_wave = false, has_current = false;
  for (const auto& p : m.params) {
    if (p == "waves" || p == "swell" || p == "wind-waves") has_wave = true;
    if (p == "currents") has_current = true;
  }
  if (has_wave) return "wave";
  if (has_current) return "current";
  return "atmosphere";
}

static wxString FmtDeg(double v) { return wxString::Format("%.4f", v); }

static wxString FormatModelLabel(const WfModel& m) {
  // Emit letters in the fixed canonical order regardless of the order in
  // m.params. Each slot is emitted at most once (V/U slots fold multiple
  // aliases).
  std::set<wxString> have(m.params.begin(), m.params.end());
  wxString params;
  for (const auto& slot : PARAM_SLOTS) {
    for (const auto* k : slot.keys) {
      if (!k) break;
      if (have.find(k) != have.end()) {
        if (!params.IsEmpty()) params += ".";
        params += slot.code;
        break;
      }
    }
  }
  return wxString::Format("%s  -  %dh, [%s]",
                          m.name.IsEmpty() ? m.id : m.name,
                          m.forecast_horizon_h, params);
}

// Position of param `p` in PARAM_SLOTS (canonical W G P C A I R T H V U S
// order). Unknown params go to the end.
static int ParamCanonicalIndex(const wxString& p) {
  const int n = static_cast<int>(sizeof(PARAM_SLOTS) / sizeof(PARAM_SLOTS[0]));
  for (int i = 0; i < n; ++i) {
    for (const char* k : PARAM_SLOTS[i].keys) {
      if (!k) break;
      if (p == k) return i;
    }
  }
  return 1000;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

WfMultiSliceDialog::WfMultiSliceDialog(wxWindow* parent, const wxString& token,
                                       const WfBBox& default_box,
                                       weatherfiles_pi* owner)
    : wxDialog(parent, wxID_ANY, _("WeatherFiles multi-slice download"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_token(token),
      m_api(token),
      m_owner(owner),
      m_default_box(default_box),
      m_animTimer(this, ID_WF_MS_ANIM_TIMER) {
  auto* top = new wxBoxSizer(wxVERTICAL);

  // Header: step title
  m_stepTitle = new wxStaticText(this, wxID_ANY, _("Step 1 of 3 - Area"));
  wxFont hf = m_stepTitle->GetFont();
  hf.MakeBold();
  hf.SetPointSize(hf.GetPointSize() + 1);
  m_stepTitle->SetFont(hf);
  top->Add(m_stepTitle, 0, wxALL, 12);

  // Body: wxSimplebook with 4 pages (start + 3 wizard steps).
  m_book = new wxSimplebook(this, wxID_ANY);
  m_book->AddPage(BuildStartPage(), _("Start"));
  m_book->AddPage(BuildAreaPage(), _("Area"));
  m_book->AddPage(BuildModelsPage(), _("Models"));
  m_book->AddPage(BuildParamsPage(), _("Filters"));
  top->Add(m_book, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);

  // Global status line
  m_globalStatus = new wxStaticText(this, wxID_ANY, wxEmptyString);
  top->Add(m_globalStatus, 0, wxLEFT | wxRIGHT | wxTOP, 12);

  // Footer: Back / spacer / Next / Close
  auto* foot = new wxBoxSizer(wxHORIZONTAL);
  m_backBtn = new wxButton(this, ID_WF_MS_BACK, _("< Back"));
  foot->Add(m_backBtn, 0, wxRIGHT, 8);
  foot->AddStretchSpacer();
  m_nextBtn = new wxButton(this, ID_WF_MS_NEXT, _("Next >"));
  foot->Add(m_nextBtn, 0, wxRIGHT, 8);
  m_closeBtn = new wxButton(this, wxID_CANCEL, _("Close"));
  foot->Add(m_closeBtn, 0);
  top->Add(foot, 0, wxEXPAND | wxALL, 12);

  SetSizerAndFit(top);
  SetMinSize(wxSize(680, 600));

  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnBack, this, ID_WF_MS_BACK);
  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnNext, this, ID_WF_MS_NEXT);
  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnDownload, this, ID_WF_MS_DOWNLOAD);
  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnUseView, this, ID_WF_MS_USEVIEW);
  Bind(wxEVT_BUTTON, &WfMultiSliceDialog::OnDraw, this, ID_WF_MS_DRAW);
  Bind(wxEVT_CLOSE_WINDOW, &WfMultiSliceDialog::OnClose, this);
  Bind(wxEVT_TIMER, &WfMultiSliceDialog::OnAnimTick, this, ID_WF_MS_ANIM_TIMER);
  // Route Close button (wxID_CANCEL) through Close() so OnClose() can Destroy.
  Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); }, wxID_CANCEL);
  Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { GotoStep(STEP_AREA); },
       ID_WF_MS_NEW_AREA);

  GotoStep(STEP_START);
  RefreshDirLetters();
  RefreshAreaDims();
  CallAfter([this]() { Load(); });
}

WfMultiSliceDialog::~WfMultiSliceDialog() {
  if (m_owner) m_owner->ClearMultiDialog(this);
}

void WfMultiSliceDialog::OnClose(wxCloseEvent&) {
  Destroy();
}

// ---------------------------------------------------------------------------
// Step 0 - Start screen (saved sets/tags)
// ---------------------------------------------------------------------------

wxPanel* WfMultiSliceDialog::BuildStartPage() {
  auto* page = new wxPanel(m_book ? (wxWindow*)m_book : (wxWindow*)this);
  auto* outer = new wxBoxSizer(wxVERTICAL);

  // Top row: title text + "Download something else (new area)" button
  // sitting right next to it, so picking a saved set vs. starting a fresh
  // area-first wizard are visually adjacent decisions.
  auto* topRow = new wxBoxSizer(wxHORIZONTAL);
  auto* titleText = new wxStaticText(
      page, wxID_ANY, _("Download a saved set (below) or"));
  topRow->Add(titleText, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  auto* newBtn = new wxButton(page, ID_WF_MS_NEW_AREA,
                              _("Download something else (new area) >>"));
  wxFont nb = newBtn->GetFont();
  nb.MakeBold();
  newBtn->SetFont(nb);
  topRow->Add(newBtn, 0, wxALIGN_CENTER_VERTICAL);
  outer->Add(topRow, 0, wxEXPAND | wxALL, 8);

  // Labeled box around the saved-sets list; label set dynamically in
  // PopulateStartPage to "Your N saved sets on WeatherFiles.com:".
  auto* sbs = new wxStaticBoxSizer(wxVERTICAL, page,
                                   _("Loading saved sets..."));
  m_startListBox = sbs->GetStaticBox();
  m_startBody = new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                                     wxSize(620, 320), wxVSCROLL);
  m_startBody->SetScrollRate(0, 12);
  m_startBodySizer = new wxBoxSizer(wxVERTICAL);
  m_startBody->SetSizer(m_startBodySizer);
  sbs->Add(m_startBody, 1, wxEXPAND | wxALL, 6);
  outer->Add(sbs, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  // Error fallback line (empty unless LoadSlices fails).
  m_startStatus = new wxStaticText(page, wxID_ANY, wxEmptyString);
  outer->Add(m_startStatus, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);

  page->SetSizer(outer);
  return page;
}

void WfMultiSliceDialog::LoadSlices() {
  if (m_startListBox) m_startListBox->SetLabel(_("Loading saved sets..."));
  if (m_startStatus) m_startStatus->SetLabel(wxEmptyString);
  m_api.FetchSlices([this](bool ok, const std::vector<WfSlice>& slices,
                           const wxString& err) {
    if (!ok) {
      if (m_startStatus)
        m_startStatus->SetLabel(_("Failed to load saved sets: ") + err);
      if (m_startListBox)
        m_startListBox->SetLabel(_("Saved sets"));
      return;
    }
    m_slices = slices;
    PopulateStartPage();
  });
}

void WfMultiSliceDialog::PopulateStartPage() {
  if (!m_startBodySizer) return;
  m_startBodySizer->Clear(true);

  // Group by tag (alphabetical via std::map). Untagged slices listed
  // individually after tagged groups, sorted by label.
  std::map<wxString, std::vector<WfSlice*>> tagged;
  std::vector<WfSlice*> untagged;
  for (auto& s : m_slices) {
    if (s.tags.empty()) {
      untagged.push_back(&s);
    } else {
      for (const auto& t : s.tags) tagged[t].push_back(&s);
    }
  }
  std::sort(untagged.begin(), untagged.end(),
            [](const WfSlice* a, const WfSlice* b) {
              return a->label.Cmp(b->label) < 0;
            });

  // No saved sets - skip the start screen entirely, drop straight into the
  // area-first wizard (Step 1). CallAfter so the GotoStep runs after this
  // populate call unwinds.
  if (tagged.empty() && untagged.empty()) {
    CallAfter([this]() { GotoStep(STEP_AREA); });
    return;
  }

  // Each row: "[Download] <name> - N slice(s)" - primary action first so the
  // download button is closest to the eye, then the descriptive label.
  // Hover the name for the model list via tooltip.
  auto add_row = [this](const wxString& name, int count,
                        const wxString& tooltip,
                        const std::vector<WfSlice*>& group,
                        const wxString& batch_label) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* dlBtn = new wxButton(m_startBody, wxID_ANY, _("Download"));
    std::vector<WfSlice*> g = group;
    wxString bl = batch_label;
    dlBtn->Bind(wxEVT_BUTTON, [this, g, bl](wxCommandEvent&) {
      RunSliceDownloads(g, bl);
    });
    row->Add(dlBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    const wxString line =
        wxString::Format(_("%s  -  %d slice%s"), name, count,
                         count == 1 ? wxString("") : wxString("s"));
    auto* info = new wxStaticText(m_startBody, wxID_ANY, line);
    if (!tooltip.IsEmpty()) info->SetToolTip(tooltip);
    row->Add(info, 1, wxALIGN_CENTER_VERTICAL);
    m_startBodySizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 6);
  };

  // Build a model_id -> display name lookup once so the tooltip shows nice
  // human names (and not just model ids) when models are loaded.
  std::map<wxString, wxString> name_of;
  for (const auto& r : m_rows) {
    name_of[r.model.id] = r.model.name.IsEmpty() ? r.model.id : r.model.name;
  }
  auto pretty_model = [&name_of](const WfSlice* s) {
    auto it = name_of.find(s->model_id);
    return it != name_of.end() ? it->second : s->model_id;
  };

  for (const auto& kv : tagged) {
    const wxString& tag = kv.first;
    const auto& group = kv.second;
    wxString tt;   // newline-separated, one model per line
    for (const auto* s : group) {
      if (!tt.IsEmpty()) tt += "\n";
      tt += pretty_model(s);
    }
    add_row(tag, static_cast<int>(group.size()), tt, group, tag);
  }
  for (WfSlice* s : untagged) {
    const wxString name =
        s->label.IsEmpty() ? wxString(_("(untagged slice)")) : s->label;
    add_row(name, 1, pretty_model(s), {s},
            s->label.IsEmpty() ? s->model_id : s->label);
  }

  if (m_startListBox)
    m_startListBox->SetLabel(wxString::Format(
        _("Your %zu saved set(s) on WeatherFiles.com:"),
        tagged.size() + untagged.size()));
  if (m_startStatus) m_startStatus->SetLabel(wxEmptyString);
  m_startBody->FitInside();
  m_startBody->Layout();
  Layout();
}

void WfMultiSliceDialog::RunSliceDownloads(
    const std::vector<WfSlice*>& slices, const wxString& batch_label) {
  if (slices.empty()) return;

  // Per-batch output dir under ~/Documents/WeatherFiles/<tag>_<ts>/.
  const wxString ts = wxDateTime::Now().Format("%Y-%m-%dT%H%M");
  wxString base = wxFileName(wxStandardPaths::Get().GetDocumentsDir(), "")
                      .GetPathWithSep() +
                  "WeatherFiles";
  if (!wxDirExists(base))
    wxFileName::Mkdir(base, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  wxString tag_safe = batch_label;
  tag_safe.Replace(" ", "_");
  tag_safe.Replace("/", "-");
  wxString dir =
      wxFileName(base, tag_safe + "_" + ts).GetFullPath();
  if (!wxDirExists(dir))
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  // Build a model_id -> category lookup from the catalogue we already
  // fetched. If a slice's model isn't in the catalogue (e.g. deactivated),
  // default to "atmosphere" so the grib_pi handoff still picks something.
  std::map<wxString, wxString> cat_of;
  for (const auto& r : m_rows) cat_of[r.model.id] = r.category;

  std::vector<WfDownloadJob> jobs;
  for (WfSlice* s : slices) {
    WfDownloadJob j;
    const wxString name =
        s->label.IsEmpty() ? s->model_id : s->label;
    auto it = cat_of.find(s->model_id);
    j.category = (it != cat_of.end()) ? it->second : wxString("atmosphere");
    j.label = name + " (" + j.category + ")";
    j.query = "/dl/" + s->token;
    // Slugged file name to avoid collisions across slices with same model.
    wxString fn = name;
    fn.Replace(" ", "_");
    fn.Replace("/", "-");
    j.out_path = wxFileName(dir, fn + ".grib2").GetFullPath();
    jobs.push_back(j);
  }

  {
    // Slice replay: these are already-saved sets, so disable the
    // Save-as-set section (can_save_as_set=false) and skip the existing-tags
    // list entirely.
    WfDownloadProgress dlg(this, m_token, std::move(jobs), {},
                           /*can_save_as_set=*/false);
    dlg.ShowModal();
  }
  Close();   // "When all is said and done... the whole plugin closes."
}

// ---------------------------------------------------------------------------
// Step 1 - Area
// ---------------------------------------------------------------------------

wxPanel* WfMultiSliceDialog::BuildAreaPage() {
  auto* page = new wxPanel(m_book ? (wxWindow*)m_book : (wxWindow*)this);

  auto* outer = new wxBoxSizer(wxVERTICAL);

  // Choice header: explain the two ways to set the area.
  auto* choice = new wxStaticText(
      page, wxID_ANY,
      _("Either type a bounding box below, use the current chart view, or "
        "drag-draw one on the chart. Then continue to Step 2."));
  choice->Wrap(620);
  outer->Add(choice, 0, wxALL, 8);

  // 3x3 grid: N on top, S on bottom, W left, E right, rectangle centre.
  auto* gbs = new wxGridBagSizer(6, 6);

  double n0 = m_default_box.valid ? m_default_box.north : 50.0;
  double s0 = m_default_box.valid ? m_default_box.south : 42.0;
  double w0 = m_default_box.valid ? m_default_box.west : -5.0;
  double e0 = m_default_box.valid ? m_default_box.east : 10.0;

  auto make_field =
      [this, page](wxTextCtrl*& field, wxStaticText*& letter, double v) {
        field = new wxTextCtrl(page, wxID_ANY, FmtDeg(v), wxDefaultPosition,
                               wxSize(90, -1));
        letter = new wxStaticText(page, wxID_ANY, " N");
        wxFont lf = letter->GetFont();
        lf.MakeBold();
        letter->SetFont(lf);
        field->Bind(wxEVT_TEXT, [this](wxCommandEvent& e) {
          OnAreaFieldChanged(e);
        });
      };

  make_field(m_north, m_nLetter, n0);
  make_field(m_south, m_sLetter, s0);
  make_field(m_west, m_wLetter, w0);
  make_field(m_east, m_eLetter, e0);

  // Cell builders: input + cardinal letter.
  auto cell = [](wxTextCtrl* tc, wxStaticText* lt) {
    auto* h = new wxBoxSizer(wxHORIZONTAL);
    h->Add(tc, 0, wxALIGN_CENTER_VERTICAL);
    h->Add(lt, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
    return h;
  };

  // (row 0, col 1) - N input, centred horizontally, sitting just above the
  // rectangle (BOTTOM-aligned).
  gbs->Add(cell(m_north, m_nLetter), wxGBPosition(0, 1),
           wxGBSpan(1, 1),
           wxALIGN_CENTER_HORIZONTAL | wxALIGN_BOTTOM | wxBOTTOM, 2);

  // (row 1, col 0) - W input, right-aligned, flush against the rectangle.
  gbs->Add(cell(m_west, m_wLetter), wxGBPosition(1, 0),
           wxGBSpan(1, 1),
           wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);

  // (row 1, col 2) - E input, left-aligned, flush against the rectangle.
  gbs->Add(cell(m_east, m_eLetter), wxGBPosition(1, 2),
           wxGBSpan(1, 1),
           wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);

  // (row 2, col 1) - S input, centred horizontally, sitting just below the
  // rectangle (TOP-aligned).
  gbs->Add(cell(m_south, m_sLetter), wxGBPosition(2, 1),
           wxGBSpan(1, 1),
           wxALIGN_CENTER_HORIZONTAL | wxALIGN_TOP | wxTOP, 2);

  // (row 1, col 1) - the visual rectangle. Transparent body (inherits the
  // page background), just a simple border. Smaller than before so the W/E
  // inputs sit closer to the edges. The dimensions readout is wrapped in a
  // HORIZONTAL stretch-spacer sandwich so the multi-line text is reliably
  // centered on macOS / GTK / MSW.
  auto* rect = new wxPanel(page, wxID_ANY, wxDefaultPosition, wxSize(220, 140),
                           wxBORDER_SIMPLE);
  auto* rs = new wxBoxSizer(wxVERTICAL);
  rs->AddStretchSpacer();
  m_areaDims = new wxStaticText(rect, wxID_ANY, _("(area)"),
                                wxDefaultPosition, wxDefaultSize,
                                wxALIGN_CENTRE_HORIZONTAL);
  // (Bold-only - macOS strips SetForegroundColour on wxStaticText; the
  // bold weight is enough to make the dims read clearly inside the box.)
  wxFont df = m_areaDims->GetFont();
  df.MakeBold();
  df.SetPointSize(df.GetPointSize() + 1);
  m_areaDims->SetFont(df);
  auto* center = new wxBoxSizer(wxHORIZONTAL);
  center->AddStretchSpacer();
  center->Add(m_areaDims, 0, wxALIGN_CENTER_VERTICAL);
  center->AddStretchSpacer();
  rs->Add(center, 0, wxEXPAND | wxALL, 8);
  rs->AddStretchSpacer();
  rect->SetSizer(rs);
  gbs->Add(rect, wxGBPosition(1, 1), wxGBSpan(1, 1), wxEXPAND);

  gbs->AddGrowableCol(1, 1);
  gbs->AddGrowableRow(1, 1);

  outer->Add(gbs, 1, wxEXPAND | wxLEFT | wxRIGHT, 24);

  // Action row: Use chart view / Draw on chart. Use-chart-view disables
  // itself after a press; OnAreaFieldChanged re-enables it whenever the
  // user edits a field (so it goes back to "press to revert to view").
  auto* arow = new wxBoxSizer(wxHORIZONTAL);
  arow->AddStretchSpacer();
  m_useViewBtn = new wxButton(page, ID_WF_MS_USEVIEW, _("Use chart view"));
  arow->Add(m_useViewBtn, 0, wxRIGHT, 8);
  arow->Add(new wxButton(page, ID_WF_MS_DRAW, _("Draw on chart")), 0);
  arow->AddStretchSpacer();
  outer->Add(arow, 0, wxEXPAND | wxALL, 12);

  m_areaStatus = new wxStaticText(page, wxID_ANY, wxEmptyString);
  outer->Add(m_areaStatus, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

  page->SetSizer(outer);
  return page;
}

void WfMultiSliceDialog::OnAreaFieldChanged(wxCommandEvent&) {
  RefreshDirLetters();
  RefreshAreaDims();
  // Any manual edit re-enables "Use chart view" (it sits next to the inputs
  // and only makes sense once the value has diverged from the chart view).
  if (m_useViewBtn) m_useViewBtn->Enable();
}

void WfMultiSliceDialog::RefreshDirLetters() {
  if (!m_nLetter || !m_sLetter || !m_wLetter || !m_eLetter) return;
  const double n = FieldVal(m_north, 0);
  const double s = FieldVal(m_south, 0);
  const double w = FieldVal(m_west, 0);
  const double e = FieldVal(m_east, 0);
  m_nLetter->SetLabel(n >= 0 ? " N" : " S");
  m_sLetter->SetLabel(s >= 0 ? " N" : " S");
  m_wLetter->SetLabel(w >= 0 ? " E" : " W");
  m_eLetter->SetLabel(e >= 0 ? " E" : " W");
}

void WfMultiSliceDialog::RefreshAreaDims() {
  if (!m_areaDims) return;
  const double n = FieldVal(m_north, 0);
  const double s = FieldVal(m_south, 0);
  const double w = FieldVal(m_west, 0);
  const double e = FieldVal(m_east, 0);
  const double dlat = n - s;
  const double dlon = e - w;
  if (dlat <= 0 || dlon <= 0) {
    m_areaDims->SetLabel(_("(invalid area)"));
    return;
  }
  const double mid_lat = (n + s) * 0.5;
  // 1 degree of latitude = 60 nautical miles; longitude scales with cos(lat).
  const double nm_lat = dlat * 60.0;
  const double nm_lon = dlon * 60.0 * std::cos(mid_lat * WF_PI / 180.0);
  // Display as "width x height" (matches W x H rectangle convention):
  //   - width  = longitudinal span (dlon, nm_lon)
  //   - height = latitudinal  span (dlat, nm_lat)
  // ASCII "deg" instead of the degree sign - MSVC C4819 trips on it.
  m_areaDims->SetLabel(
      wxString::Format(_("%.2f deg x %.2f deg\nca. %d x %d nm"), dlon, dlat,
                       static_cast<int>(nm_lon), static_cast<int>(nm_lat)));
}

void WfMultiSliceDialog::OnUseView(wxCommandEvent&) {
  if (!m_default_box.valid) {
    m_areaStatus->SetLabel(_("No chart view available."));
    return;
  }
  SetBox(m_default_box);
  // Disable so the user can't re-apply the same view twice; any field edit
  // re-enables it via OnAreaFieldChanged.
  if (m_useViewBtn) m_useViewBtn->Disable();
}

void WfMultiSliceDialog::OnDraw(wxCommandEvent&) {
  if (!m_owner) return;
  // No status text - the slide animation is the cue that the chart is now
  // waiting for input.
  weatherfiles_pi* owner = m_owner;
  owner->StartAreaPickMulti([owner](const WfBBox& box) {
    if (auto* dlg = owner->MultiDialog()) {
      dlg->SetBox(box);
      dlg->StartSlide(false);  // restore
    }
  });
  StartSlide(true);  // minimize
}

void WfMultiSliceDialog::SetBox(const WfBBox& box) {
  if (!box.valid) return;
  m_north->SetValue(FmtDeg(box.north));
  m_south->SetValue(FmtDeg(box.south));
  m_west->SetValue(FmtDeg(box.west));
  m_east->SetValue(FmtDeg(box.east));
  RefreshDirLetters();
  RefreshAreaDims();
  // Don't disable m_useViewBtn here. SetValue fires wxEVT_TEXT which routes
  // through OnAreaFieldChanged and re-enables the button - that's exactly
  // what we want for a rubber-band drag-draw (the values have diverged from
  // the chart view, so the button should be live). For the "Use chart view"
  // path, OnUseView calls SetBox then disables the button itself - the box
  // _is_ the chart view in that case, so further presses would be no-ops.
  Raise();
}

// ---------------------------------------------------------------------------
// Slide animation
// ---------------------------------------------------------------------------

void WfMultiSliceDialog::StartSlide(bool toMinimized) {
  // Compute home + min positions on first use of this run. The "min" leaves
  // a 32-px sliver on the right edge of the screen so the user can see where
  // the dialog went.
  wxRect screen = wxDisplay(wxDisplay::GetFromWindow(this)).GetClientArea();
  if (!toMinimized && m_animHome == wxPoint(0, 0)) {
    // Restoring with no saved home (shouldn't happen but be safe).
    m_animHome = GetPosition();
  }
  if (toMinimized) {
    m_animHome = GetPosition();
    m_animMin = wxPoint(screen.GetRight() - 32, m_animHome.y);
  }
  m_animToMinimized = toMinimized;
  m_animStep = 0;
  m_animActive = true;
  m_animTimer.Start(16);
}

void WfMultiSliceDialog::OnAnimTick(wxTimerEvent&) {
  if (!m_animActive) {
    m_animTimer.Stop();
    return;
  }
  ++m_animStep;
  double t = static_cast<double>(m_animStep) / m_animTotal;
  if (t >= 1.0) {
    t = 1.0;
    m_animActive = false;
    m_animTimer.Stop();
  }
  // Smoothstep ease: 3t² - 2t³.
  const double e = t * t * (3.0 - 2.0 * t);
  const wxPoint from = m_animToMinimized ? m_animHome : m_animMin;
  const wxPoint to = m_animToMinimized ? m_animMin : m_animHome;
  const int x = from.x + static_cast<int>((to.x - from.x) * e);
  const int y = from.y + static_cast<int>((to.y - from.y) * e);
  Move(x, y);
}

// ---------------------------------------------------------------------------
// Step 2 - Models
// ---------------------------------------------------------------------------

wxPanel* WfMultiSliceDialog::BuildModelsPage() {
  auto* page = new wxPanel(m_book ? (wxWindow*)m_book : (wxWindow*)this);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  auto* hint = new wxStaticText(
      page, wxID_ANY,
      _("Pick up to 5 atmosphere + 2 wave + 2 current. Coverage badges "
        "reflect the bounding box from Step 1."));
  hint->Wrap(620);
  outer->Add(hint, 0, wxALL, 8);

  m_modelsBody =
      new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                           wxSize(620, 360), wxVSCROLL);
  m_modelsBody->SetScrollRate(0, 12);
  m_modelsBodySizer = new wxBoxSizer(wxVERTICAL);
  m_modelsBody->SetSizer(m_modelsBodySizer);
  outer->Add(m_modelsBody, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);

  m_modelsStatus = new wxStaticText(page, wxID_ANY, _("Loading models..."));
  outer->Add(m_modelsStatus, 0, wxALL, 12);

  page->SetSizer(outer);
  return page;
}

void WfMultiSliceDialog::Load() {
  if (m_modelsStatus) m_modelsStatus->SetLabel(_("Loading models..."));
  wxBusyCursor busy;
  m_api.ValidateToken([this](bool ok, const WfAccount& acct,
                             const wxString& err) {
    if (!ok) {
      if (m_modelsStatus)
        m_modelsStatus->SetLabel(_("Token error: ") + err);
      return;
    }
    m_allowed = acct.allowed_models;
    m_api.FetchModels([this, acct](bool ok2, const std::vector<WfModel>& models,
                                   const wxString& err2) {
      if (!ok2) {
        if (m_modelsStatus)
          m_modelsStatus->SetLabel(_("Failed to load models: ") + err2);
        return;
      }
      m_rows.clear();
      m_rows.reserve(models.size());
      for (const auto& m : models) {
        if (m.format != "grib" && !m.format.IsEmpty()) continue;
        ModelRow r;
        r.model = m;
        r.category = CategorizeModel(m);
        m_rows.push_back(r);
      }
      PopulateModelsPage();
      RecomputeCoverage();
      RefreshModelsStatus();
      if (m_globalStatus)
        m_globalStatus->SetLabel(
            wxString::Format(_("Signed in as %s - %s tier"), acct.email,
                             acct.tier));
      // Saved-sets list depends on the model catalogue (for the per-slice
      // category lookup at download time), so chain after models load.
      LoadSlices();
    });
  });
}

void WfMultiSliceDialog::PopulateModelsPage() {
  if (!m_modelsBodySizer) return;
  m_modelsBodySizer->Clear(true);

  struct Sec {
    wxString title;
    wxString cat;
    int cap;
  };
  const Sec sections[] = {
      {_("Atmosphere"), "atmosphere", ATMO_CAP},
      {_("Waves"), "wave", WAVE_CAP},
      {_("Currents"), "current", CURR_CAP},
  };

  for (const auto& sec : sections) {
    auto* box = new wxStaticBoxSizer(
        wxVERTICAL, m_modelsBody,
        wxString::Format(_("%s (up to %d)"), sec.title, sec.cap));
    m_modelsBodySizer->Add(box, 0, wxEXPAND | wxALL, 6);

    std::vector<ModelRow*> in_cat;
    for (auto& r : m_rows)
      if (r.category == sec.cat) in_cat.push_back(&r);
    std::sort(in_cat.begin(), in_cat.end(),
              [](const ModelRow* a, const ModelRow* b) {
                return a->model.name.Cmp(b->model.name) < 0;
              });

    if (in_cat.empty()) {
      auto* none = new wxStaticText(m_modelsBody, wxID_ANY, _("  (none)"));
      none->SetForegroundColour(COL_OUTSIDE);
      box->Add(none, 0, wxLEFT | wxTOP | wxBOTTOM, 4);
      continue;
    }

    for (ModelRow* r : in_cat) {
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      const bool allowed =
          m_allowed.empty() ||
          std::find(m_allowed.begin(), m_allowed.end(), r->model.id) !=
              m_allowed.end();

      // Dot + plain text indicator FIRST, on the left. Putting it before the
      // checkbox guarantees it stays visible no matter how long the model
      // label is (the checkbox takes the proportion-1 flex slot). Circle
      // bitmap, parent's bg colour as the canvas so the square corners
      // blend in.
      const wxColour body_bg = m_modelsBody->GetBackgroundColour();
      r->coverage_dot = new wxStaticBitmap(
          m_modelsBody, wxID_ANY,
          MakeColorCircle(COL_OUTSIDE, body_bg));
      row->Add(r->coverage_dot, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
      r->coverage_text =
          new wxStaticText(m_modelsBody, wxID_ANY, _("(coverage)"));
      wxFont cf = r->coverage_text->GetFont();
      cf.MakeBold();
      r->coverage_text->SetFont(cf);
      row->Add(r->coverage_text, 0,
               wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 4);

      wxString label = FormatModelLabel(r->model);
      if (!allowed) label += _("  (Pro tier)");
      r->check = new wxCheckBox(m_modelsBody, wxID_ANY, label);
      if (!allowed) r->check->Disable();
      r->check->Bind(wxEVT_CHECKBOX,
                     [this](wxCommandEvent& e) { OnModelCheck(e); });
      row->Add(r->check, 1, wxALIGN_CENTER_VERTICAL);
      box->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 4);
    }
  }

  m_modelsBody->FitInside();
  m_modelsBody->Layout();
}

void WfMultiSliceDialog::OnModelCheck(wxCommandEvent& evt) {
  auto* src = wxDynamicCast(evt.GetEventObject(), wxCheckBox);
  if (!src) return;
  if (!src->IsChecked()) {
    RefreshModelsStatus();
    return;
  }
  wxString cat;
  for (const auto& r : m_rows) {
    if (r.check == src) {
      cat = r.category;
      break;
    }
  }
  const int cap = (cat == "atmosphere") ? ATMO_CAP
                  : (cat == "wave")     ? WAVE_CAP
                  : (cat == "current")  ? CURR_CAP
                                        : 0;
  if (SelectedCount(cat) > cap) {
    src->SetValue(false);
    if (m_modelsStatus)
      m_modelsStatus->SetLabel(wxString::Format(
          _("Maximum %d %s model(s) - uncheck one first."), cap, cat));
    return;
  }
  RefreshModelsStatus();
}

void WfMultiSliceDialog::RecomputeCoverage() {
  const wxColour body_bg =
      m_modelsBody ? m_modelsBody->GetBackgroundColour() : wxNullColour;
  for (auto& r : m_rows) {
    if (!r.check || !r.coverage_dot || !r.coverage_text) continue;
    r.state = CoverageFor(r.model);
    const bool allowed_tier =
        m_allowed.empty() ||
        std::find(m_allowed.begin(), m_allowed.end(), r.model.id) !=
            m_allowed.end();
    wxColour c;
    wxString label;
    bool enable = allowed_tier;
    switch (r.state) {
      case COV_FULL:    c = COL_FULL;    label = _("(full)");    break;
      case COV_PARTIAL: c = COL_PARTIAL; label = _("(partial)"); break;
      case COV_OUTSIDE: c = COL_OUTSIDE; label = _("(outside)");
                        if (r.check->IsChecked()) r.check->SetValue(false);
                        enable = false;
                        break;
    }
    r.coverage_dot->SetBitmap(MakeColorCircle(c, body_bg));
    r.coverage_text->SetLabel(label);
    r.check->Enable(enable);
  }
}

void WfMultiSliceDialog::RefreshModelsStatus() {
  if (!m_modelsStatus) return;
  const int a = SelectedCount("atmosphere");
  const int w = SelectedCount("wave");
  const int c = SelectedCount("current");
  if (a + w + c == 0) {
    m_modelsStatus->SetLabel(
        _("Pick up to 5 atmosphere + 2 wave + 2 current."));
  } else {
    m_modelsStatus->SetLabel(wxString::Format(
        _("%d atmosphere + %d wave + %d current selected."), a, w, c));
  }
}

// ---------------------------------------------------------------------------
// Step 3 - Params + filters
// ---------------------------------------------------------------------------

wxPanel* WfMultiSliceDialog::BuildParamsPage() {
  auto* page = new wxPanel(m_book ? (wxWindow*)m_book : (wxWindow*)this);
  auto* outer = new wxBoxSizer(wxVERTICAL);
  auto* hint = new wxStaticText(
      page, wxID_ANY,
      _("Parameters reflect the union of your selected models. All defaults "
        "are on; uncheck to skip. Each model receives only the params it "
        "offers."));
  hint->Wrap(620);
  outer->Add(hint, 0, wxALL, 8);

  // Visually-bundled "Parameters" box matching the Filters section below.
  auto* paramsBox = new wxStaticBoxSizer(wxVERTICAL, page, _("Parameters"));
  m_paramsBody =
      new wxScrolledWindow(page, wxID_ANY, wxDefaultPosition,
                           wxSize(620, 140), wxVSCROLL);
  m_paramsBody->SetScrollRate(0, 12);
  m_paramsBodySizer = new wxBoxSizer(wxVERTICAL);
  m_paramsBody->SetSizer(m_paramsBodySizer);
  paramsBox->Add(m_paramsBody, 1, wxEXPAND | wxALL, 6);
  outer->Add(paramsBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 8);

  // Bundled "Filters" - horizon / step / spatial inline on one row, each
  // with a click-to-open "(?)" help tooltip. Names match the web naming.
  auto* filtersBox = new wxStaticBoxSizer(wxHORIZONTAL, page, _("Filters"));

  auto add_help = [page](wxSizer* dest, const wxString& tip) {
    auto* q = new wxStaticText(page, wxID_ANY, " (?)");
    wxFont qf = q->GetFont();
    qf.MakeBold();
    q->SetFont(qf);
    q->SetCursor(wxCursor(wxCURSOR_HAND));
    // Hover-tooltip kept as a fallback (works on Linux/Win); macOS often
    // ignores it on wxStaticText, so we also pop a wxRichToolTip on click.
    q->SetToolTip(tip);
    wxString tipCopy = tip;
    q->Bind(wxEVT_LEFT_DOWN, [tipCopy, q](wxMouseEvent&) {
      wxRichToolTip rt(_("Help"), tipCopy);
      rt.SetIcon(wxICON_INFORMATION);
      rt.SetTimeout(10000);
      rt.ShowFor(q);
    });
    dest->Add(q, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  };

  filtersBox->AddSpacer(6);
  filtersBox->Add(new wxStaticText(page, wxID_ANY, _("horizon:")), 0,
                  wxALIGN_CENTER_VERTICAL);
  m_window = new wxSpinCtrl(page, wxID_ANY, "48", wxDefaultPosition,
                            wxSize(72, -1), wxSP_ARROW_KEYS, 1, 384, 48);
  filtersBox->Add(m_window, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  add_help(filtersBox,
           _("Forecast horizon in hours. Capped at the smallest selected "
             "model's native horizon."));

  filtersBox->Add(new wxStaticText(page, wxID_ANY, _("step:")), 0,
                  wxALIGN_CENTER_VERTICAL);
  // step dropdown - choices recomputed in RecomputeTimeRanges to exclude
  // values smaller than the smallest selected model's native step.
  m_stepH = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxSize(80, -1));
  filtersBox->Add(m_stepH, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  add_help(filtersBox,
           _("Time between forecast steps. Smaller step = more frames + "
             "bigger file. Values below the smallest model's native step "
             "are filtered out."));

  filtersBox->Add(new wxStaticText(page, wxID_ANY, _("spatial:")), 0,
                  wxALIGN_CENTER_VERTICAL);
  m_spatial = new wxChoice(page, wxID_ANY, wxDefaultPosition, wxSize(64, -1));
  const wxString sp[] = {"1x", "2x", "3x", "4x"};
  for (const auto& s : sp) m_spatial->Append(s);
  m_spatial->SetSelection(0);
  filtersBox->Add(m_spatial, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 4);
  add_help(filtersBox,
           _("Spatial downsample factor. 1x = native resolution; higher = "
             "coarser grid + smaller file (cell area scales 1/N^2)."));

  filtersBox->AddStretchSpacer();
  outer->Add(filtersBox, 0, wxEXPAND | wxALL, 8);

  m_paramsStatus = new wxStaticText(page, wxID_ANY, wxEmptyString);
  outer->Add(m_paramsStatus, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

  page->SetSizer(outer);
  return page;
}

void WfMultiSliceDialog::PopulateParamsPage() {
  if (!m_paramsBodySizer) return;
  m_paramsBodySizer->Clear(true);
  m_paramChecks.clear();

  // Union of params across selected models, then re-sorted into the
  // canonical PARAM_ORDER (wind, gusts, pressure, ...) so the 2-column
  // layout reads top-to-bottom in canonical order.
  std::set<wxString> seen;
  std::vector<wxString> ordered;
  for (const auto& r : m_rows) {
    if (!r.check || !r.check->IsChecked()) continue;
    for (const auto& p : r.model.params) {
      if (seen.insert(p).second) ordered.push_back(p);
    }
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const wxString& a, const wxString& b) {
              return ParamCanonicalIndex(a) < ParamCanonicalIndex(b);
            });

  if (ordered.empty()) {
    auto* none = new wxStaticText(m_paramsBody, wxID_ANY,
                                  _("(no parameters - select models first)"));
    none->SetForegroundColour(COL_OUTSIDE);
    m_paramsBodySizer->Add(none, 0, wxALL, 6);
  } else {
    // Column-major 2 columns - fills top-to-bottom in the LEFT column first,
    // then continues top-to-bottom in the RIGHT column. Sits at the top-left
    // (no EXPAND), not stretched across the full width.
    const size_t total = ordered.size();
    const size_t half = (total + 1) / 2;   // ceil(N/2) lands in col 1
    auto* hbox = new wxBoxSizer(wxHORIZONTAL);
    auto* col1 = new wxBoxSizer(wxVERTICAL);
    auto* col2 = new wxBoxSizer(wxVERTICAL);
    for (size_t i = 0; i < total; ++i) {
      auto* cb = new wxCheckBox(m_paramsBody, wxID_ANY, ordered[i]);
      cb->SetValue(true);
      ((i < half) ? col1 : col2)->Add(cb, 0, wxBOTTOM, 4);
      m_paramChecks[ordered[i]] = cb;
    }
    hbox->Add(col1, 0, wxRIGHT, 32);
    hbox->Add(col2, 0);
    m_paramsBodySizer->Add(hbox, 0, wxALL, 6);
  }
  m_paramsBody->FitInside();
  m_paramsBody->Layout();
}

void WfMultiSliceDialog::RecomputeTimeRanges() {
  int min_horizon = INT32_MAX;
  // Largest of the selected models' native steps becomes the floor for the
  // dropdown - we never offer a step finer than the coarsest model would
  // resolve. Default 1h floor when nothing is set.
  double floor_step = 0.25;
  int n = 0;
  for (const auto& r : m_rows) {
    if (!r.check || !r.check->IsChecked()) continue;
    ++n;
    if (r.model.forecast_horizon_h > 0 &&
        r.model.forecast_horizon_h < min_horizon)
      min_horizon = r.model.forecast_horizon_h;
    if (r.model.step_h > floor_step) floor_step = r.model.step_h;
  }
  if (n == 0 || min_horizon == INT32_MAX) return;

  m_window->SetRange(1, min_horizon);
  if (m_window->GetValue() > min_horizon) m_window->SetValue(min_horizon);

  // Rebuild the step dropdown - keep only values >= floor_step. The first
  // (smallest remaining) becomes the default.
  if (m_stepH) {
    const double all_steps[] = {0.25, 1.0, 3.0, 6.0};
    m_stepH->Clear();
    for (double s : all_steps) {
      if (s + 1e-6 < floor_step) continue;
      const wxString lbl = (s < 1.0)
                               ? wxString::Format("%.2fh", s)
                               : wxString::Format("%dh", static_cast<int>(s));
      m_stepH->Append(lbl);
    }
    if (m_stepH->GetCount() > 0) m_stepH->SetSelection(0);
  }

  if (m_paramsStatus)
    m_paramsStatus->SetLabel(wxString::Format(
        _("%d model(s); max forecast %d h, smallest step %.2gh."), n,
        min_horizon, floor_step));
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

void WfMultiSliceDialog::GotoStep(Step s) {
  m_step = s;
  if (m_book) m_book->ChangeSelection(static_cast<size_t>(s));

  // Title - start screen is unnumbered; numbered steps explicitly count "of 3".
  const wxString titles[] = {_("Gribs from WeatherFiles.com"),
                             _("Step 1 of 3 - Area"),
                             _("Step 2 of 3 - Models"),
                             _("Step 3 of 3 - Parameters and filters")};
  if (m_stepTitle) m_stepTitle->SetLabel(titles[s]);

  // Buttons
  if (m_backBtn) m_backBtn->Show(s != STEP_START);
  if (m_nextBtn) {
    if (s == STEP_START) {
      // Start screen: actions live in the body (per-set Download + "Download
      // something else"); no Next here.
      m_nextBtn->Hide();
    } else if (s == STEP_PARAMS) {
      m_nextBtn->Show();
      m_nextBtn->SetId(ID_WF_MS_DOWNLOAD);
      m_nextBtn->SetLabel(_("Download slices"));
    } else {
      m_nextBtn->Show();
      m_nextBtn->SetId(ID_WF_MS_NEXT);
      m_nextBtn->SetLabel(_("Next >"));
    }
  }

  // Step-specific population
  if (s == STEP_MODELS) {
    RecomputeCoverage();
    RefreshModelsStatus();
  } else if (s == STEP_PARAMS) {
    PopulateParamsPage();
    RecomputeTimeRanges();
  }
  Layout();
}

void WfMultiSliceDialog::OnBack(wxCommandEvent&) {
  if (m_step == STEP_AREA) GotoStep(STEP_START);
  else if (m_step == STEP_MODELS) GotoStep(STEP_AREA);
  else if (m_step == STEP_PARAMS) GotoStep(STEP_MODELS);
}

void WfMultiSliceDialog::OnNext(wxCommandEvent&) {
  if (m_step == STEP_AREA) {
    // Validate bbox.
    const double n = FieldVal(m_north, 0);
    const double s = FieldVal(m_south, 0);
    const double w = FieldVal(m_west, 0);
    const double e = FieldVal(m_east, 0);
    if (n <= s || e <= w) {
      m_areaStatus->SetLabel(_("Set North > South and East > West."));
      return;
    }
    GotoStep(STEP_MODELS);
  } else if (m_step == STEP_MODELS) {
    const int selected = SelectedCount("atmosphere") + SelectedCount("wave") +
                         SelectedCount("current");
    if (selected == 0) {
      m_modelsStatus->SetLabel(_("Select at least one model."));
      return;
    }
    GotoStep(STEP_PARAMS);
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

double WfMultiSliceDialog::FieldVal(wxTextCtrl* c, double fallback) const {
  double v;
  if (c && c->GetValue().ToDouble(&v)) return v;
  return fallback;
}

bool WfMultiSliceDialog::BBoxIntersects(const WfModel& m, double s, double n,
                                        double w, double e) const {
  if (n <= m.south || s >= m.north) return false;
  if (e <= m.west || w >= m.east) return false;
  return true;
}

bool WfMultiSliceDialog::BBoxFullyInside(const WfModel& m, double s, double n,
                                         double w, double e) const {
  return s >= m.south && n <= m.north && w >= m.west && e <= m.east;
}

WfMultiSliceDialog::Coverage
WfMultiSliceDialog::CoverageFor(const WfModel& m) const {
  const double n = FieldVal(m_north, 0);
  const double s = FieldVal(m_south, 0);
  const double w = FieldVal(m_west, 0);
  const double e = FieldVal(m_east, 0);
  if (!BBoxIntersects(m, s, n, w, e)) return COV_OUTSIDE;
  if (BBoxFullyInside(m, s, n, w, e)) return COV_FULL;
  return COV_PARTIAL;
}

std::vector<WfMultiSliceDialog::ModelRow*>
WfMultiSliceDialog::SelectedRows() const {
  std::vector<ModelRow*> out;
  for (auto& r : const_cast<std::vector<ModelRow>&>(m_rows)) {
    if (r.check && r.check->IsChecked()) out.push_back(&r);
  }
  return out;
}

int WfMultiSliceDialog::SelectedCount(const wxString& category) const {
  int n = 0;
  for (const auto& r : m_rows) {
    if (r.category == category && r.check && r.check->IsChecked()) ++n;
  }
  return n;
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

void WfMultiSliceDialog::OnDownload(wxCommandEvent&) {
  const double n = FieldVal(m_north, 0);
  const double s = FieldVal(m_south, 0);
  const double w = FieldVal(m_west, 0);
  const double e = FieldVal(m_east, 0);
  if (n <= s || e <= w) {
    if (m_paramsStatus)
      m_paramsStatus->SetLabel(_("Bounding box is invalid - go back to Step 1."));
    return;
  }

  // Selected params (UNION shown to user; each per-model query intersects
  // with the model's own param list).
  std::vector<wxString> user_params;
  for (const auto& p : m_paramChecks) {
    if (p.second && p.second->IsChecked()) user_params.push_back(p.first);
  }
  if (user_params.empty()) {
    if (m_paramsStatus)
      m_paramsStatus->SetLabel(_("Pick at least one parameter."));
    return;
  }
  std::set<wxString> user_set(user_params.begin(), user_params.end());

  const int spatial = m_spatial ? (m_spatial->GetSelection() + 1) : 1;
  // Parse the selected step from the dropdown label ("0.25h" / "1h" / ...).
  // Truncate-to-int for the int-only /v1/grib `time_step_h` param; a 0.25
  // selection becomes 0 - filtered out at the dropdown level for all
  // currently-shipped models, so the int truncation only matters for the
  // hypothetical sub-hour case.
  int time_step_h = 1;
  if (m_stepH && m_stepH->GetSelection() != wxNOT_FOUND) {
    wxString step_s = m_stepH->GetStringSelection();
    step_s = step_s.BeforeFirst('h');
    double step_d = 1.0;
    if (step_s.ToDouble(&step_d) && step_d >= 1.0)
      time_step_h = static_cast<int>(step_d);
  }

  // Per-batch output directory.
  const wxString ts = wxDateTime::Now().Format("%Y-%m-%dT%H%M");
  wxString base = wxFileName(wxStandardPaths::Get().GetDocumentsDir(), "")
                      .GetPathWithSep() +
                  "WeatherFiles";
  if (!wxDirExists(base))
    wxFileName::Mkdir(base, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  wxString dir = wxFileName(base, ts).GetFullPath();
  if (!wxDirExists(dir))
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  // Build jobs in send-friendly order: currents, waves, atmospheres - so
  // grib_pi's handoff makes the first atmosphere the active file.
  std::vector<WfDownloadJob> jobs;
  for (int phase = 0; phase < 3; ++phase) {
    const wxString want_cat =
        (phase == 0) ? "current" : (phase == 1) ? "wave" : "atmosphere";
    for (const auto& r : m_rows) {
      if (!r.check || !r.check->IsChecked()) continue;
      if (r.category != want_cat) continue;

      // Intersect user_params with this model's own params.
      wxString params;
      for (const auto& p : r.model.params) {
        if (user_set.find(p) == user_set.end()) continue;
        if (!params.IsEmpty()) params += ",";
        params += p;
      }
      if (params.IsEmpty()) continue;   // model offers none of the picked

      // Clip bbox to model coverage.
      const double cs = std::max(r.model.south, s);
      const double cn = std::min(r.model.north, n);
      const double cw = std::max(r.model.west, w);
      const double ce = std::min(r.model.east, e);

      WfDownloadJob j;
      j.label = r.model.name.IsEmpty() ? r.model.id : r.model.name;
      j.label += " (" + r.category + ")";
      j.category = r.category;
      const wxString bbox =
          wxString::Format("%.4f,%.4f,%.4f,%.4f", cw, ce, cs, cn);
      wxString q = wxString::Format(
          "/grib?model=%s&params=%s&bbox=%s&time_window_h=%d&time_step_h=%d",
          r.model.id, params, bbox, m_window->GetValue(), time_step_h);
      if (spatial > 1) q += wxString::Format("&spatial_factor=%d", spatial);
      j.query = q;
      j.out_path = wxFileName(dir, r.model.id + ".grib2").GetFullPath();

      // Structured fields for the post-download "Save as set" flow.
      j.model_id = r.model.id;
      for (const auto& p : r.model.params) {
        if (user_set.find(p) != user_set.end()) j.params.push_back(p);
      }
      j.bbox = bbox;
      j.time_window_h = m_window->GetValue();
      j.time_step_h = time_step_h;
      j.spatial_factor = spatial;
      jobs.push_back(j);
    }
  }
  if (jobs.empty()) {
    if (m_paramsStatus)
      m_paramsStatus->SetLabel(
          _("No matching params across the selected models."));
    return;
  }

  // Collect existing tags from the user's already-saved slices so the
  // post-download "Save as set" input can detect collisions before the user
  // hits Save.
  std::vector<wxString> existing_tags;
  for (const auto& s : m_slices) {
    for (const auto& t : s.tags) existing_tags.push_back(t);
  }

  // Hide the wizard immediately - the user committed to this download and
  // there's no "back" path from here. Show the modal download dialog on its
  // own (parented to the canvas so it's not visually orphaned), then close
  // the wizard outright once it returns.
  Hide();
  {
    WfDownloadProgress dlg(GetParent(), m_token, std::move(jobs), existing_tags,
                           /*can_save_as_set=*/true);
    dlg.ShowModal();
  }
  Close();
}
