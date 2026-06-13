// SPDX-FileCopyrightText: 2026 Bart Manuel
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WF_MULTI_SLICE_DIALOG_H
#define WF_MULTI_SLICE_DIALOG_H

#include <wx/dialog.h>
#include <wx/gdicmn.h>   // wxPoint
#include <wx/string.h>
#include <wx/timer.h>

#include <map>
#include <vector>

#include "wf_api.h"

class wxCheckBox;
class wxChoice;
class wxTextCtrl;
class wxSpinCtrl;
class wxStaticText;
class wxStaticBitmap;
class wxStaticBox;
class wxButton;
class wxScrolledWindow;
class wxSimplebook;
class wxPanel;
class wxSizer;
class weatherfiles_pi;

// Area-first multi-slice download wizard. Opens on a "start" page listing
// the user's saved sets/tags for one-click re-download; "Download something
// else" walks the three-step wizard below.
//
//   0. Start: GET /v1/slices, group by tag (alphabetical). Each tag row =
//      one download click that batches /dl/<token> calls through the queue
//      progress dialog. Untagged slices listed below the tagged groups.
//   1. Area: type or draw an N/S/W/E bbox. Inputs sit on the four sides of a
//      visual rectangle (dimensions in nm shown inside it). "Draw on chart"
//      slides the dialog off-screen-right so the chart is uncovered; the
//      pick callback slides it back. Defaults to the current chart view.
//   2. Models: 3 category sections (Atmosphere cap 3, Waves cap 1, Currents
//      cap 1). Each model row carries a coverage badge - "(full coverage)",
//      "(partial coverage)" or "(outside coverage)" (disabled) - computed
//      against the step-1 bbox.
//   3. Parameters + filters: a checkbox per param that any selected model
//      offers (UNION; per-model query intersects with the model's own list
//      at download time), forecast hours (smallest selected horizon), step,
//      spatial factor 1x..4x (downsample passed as `spatial_factor` to
//      /v1/grib).
//
// Back/Next move through the steps; the final "Download N slices" runs the
// existing WfDownloadProgress modal. When that modal returns, the wizard
// Close()s itself so the whole plugin tears down.
//
// The dialog is modeless (the chart must stay interactive during area draw,
// which a modal loop would block). The owner plugin holds the pointer and
// clears it in the dialog destructor.
class WfMultiSliceDialog : public wxDialog {
 public:
  WfMultiSliceDialog(wxWindow* parent, const wxString& token,
                     const WfBBox& default_box, weatherfiles_pi* owner);
  ~WfMultiSliceDialog() override;

  // Plugin invokes this on a rubber-band area release.
  void SetBox(const WfBBox& box);

 private:
  enum Step {
    STEP_START = 0,
    STEP_AREA = 1,
    STEP_MODELS = 2,
    STEP_PARAMS = 3,
  };
  enum Coverage { COV_FULL, COV_PARTIAL, COV_OUTSIDE };

  struct ModelRow {
    WfModel model;
    wxString category;          // "atmosphere" | "wave" | "current"
    wxCheckBox* check = nullptr;
    // Coverage indicator. wxStaticBitmap with three pre-baked solid-colour
    // bitmaps (cached via GetCovBitmap in the cpp). Lives BEFORE the
    // checkbox in the row so a long model name can't push it off-screen.
    wxStaticBitmap* coverage_dot = nullptr;
    wxStaticText* coverage_text = nullptr;
    Coverage state = COV_FULL;
  };

  // --- build ---
  wxPanel* BuildStartPage();
  wxPanel* BuildAreaPage();
  wxPanel* BuildModelsPage();
  wxPanel* BuildParamsPage();

  // --- navigation ---
  void GotoStep(Step s);
  void OnBack(wxCommandEvent&);
  void OnNext(wxCommandEvent&);

  // --- step 0: start screen ---
  void LoadSlices();                    // fires after Load() succeeds
  void PopulateStartPage();
  void RunSliceDownloads(const std::vector<WfSlice*>& slices,
                         const wxString& batch_label);

  // --- step 1: area ---
  void OnUseView(wxCommandEvent&);
  void OnDraw(wxCommandEvent&);
  void OnAreaFieldChanged(wxCommandEvent&);
  void RefreshDirLetters();
  void RefreshAreaDims();
  void StartSlide(bool toMinimized);   // true => slide off; false => restore
  void OnAnimTick(wxTimerEvent&);

  // --- step 2: models ---
  void Load();                         // ValidateToken -> FetchModels
  void PopulateModelsPage();
  void OnModelCheck(wxCommandEvent&);
  void RecomputeCoverage();            // updates badges + disables outside
  void RefreshModelsStatus();

  // --- step 3: params + filters ---
  void PopulateParamsPage();
  void RecomputeTimeRanges();

  // --- final ---
  void OnDownload(wxCommandEvent&);
  void OnClose(wxCloseEvent&);

  // --- helpers ---
  double FieldVal(wxTextCtrl* c, double fallback) const;
  bool BBoxIntersects(const WfModel& m, double s, double n, double w,
                      double e) const;
  bool BBoxFullyInside(const WfModel& m, double s, double n, double w,
                       double e) const;
  Coverage CoverageFor(const WfModel& m) const;
  std::vector<ModelRow*> SelectedRows() const;
  int SelectedCount(const wxString& category) const;

  // --- members ---
  wxString m_token;
  WfApi m_api;
  weatherfiles_pi* m_owner;
  WfBBox m_default_box;
  std::vector<wxString> m_allowed;
  std::vector<ModelRow> m_rows;
  std::vector<WfSlice> m_slices;

  Step m_step = STEP_START;

  // Frame
  wxStaticText* m_stepTitle = nullptr;
  wxSimplebook* m_book = nullptr;
  wxButton* m_backBtn = nullptr;
  wxButton* m_nextBtn = nullptr;
  wxButton* m_closeBtn = nullptr;
  wxStaticText* m_globalStatus = nullptr;

  // Step 0
  wxScrolledWindow* m_startBody = nullptr;
  wxSizer* m_startBodySizer = nullptr;
  wxStaticText* m_startStatus = nullptr;    // error fallback only
  wxStaticBox* m_startListBox = nullptr;    // 'Your N saved sets...' title

  // Step 1
  wxTextCtrl* m_north = nullptr;
  wxTextCtrl* m_south = nullptr;
  wxTextCtrl* m_west = nullptr;
  wxTextCtrl* m_east = nullptr;
  wxStaticText* m_nLetter = nullptr;
  wxStaticText* m_sLetter = nullptr;
  wxStaticText* m_wLetter = nullptr;
  wxStaticText* m_eLetter = nullptr;
  wxStaticText* m_areaDims = nullptr;
  wxStaticText* m_areaStatus = nullptr;
  wxButton* m_useViewBtn = nullptr;       // re-enabled on any field change

  // Animation
  wxTimer m_animTimer;
  wxPoint m_animHome;
  wxPoint m_animMin;
  int m_animStep = 0;
  int m_animTotal = 16;             // ticks (~250ms @ 16ms tick)
  bool m_animToMinimized = false;
  bool m_animActive = false;

  // Step 2
  wxScrolledWindow* m_modelsBody = nullptr;
  wxSizer* m_modelsBodySizer = nullptr;
  wxStaticText* m_modelsStatus = nullptr;

  // Step 3
  wxScrolledWindow* m_paramsBody = nullptr;
  wxSizer* m_paramsBodySizer = nullptr;
  std::map<wxString, wxCheckBox*> m_paramChecks;
  wxSpinCtrl* m_window = nullptr;
  wxChoice* m_stepH = nullptr;             // {0.25h, 1h, 3h, 6h} filtered
  wxChoice* m_spatial = nullptr;           // {1x, 2x, 3x, 4x}
  wxStaticText* m_paramsStatus = nullptr;
};

#endif  // WF_MULTI_SLICE_DIALOG_H
