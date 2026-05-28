#ifndef WF_MULTI_SLICE_DIALOG_H
#define WF_MULTI_SLICE_DIALOG_H

#include <wx/dialog.h>
#include <wx/string.h>

#include <vector>

#include "wf_api.h"

class wxCheckBox;
class wxTextCtrl;
class wxSpinCtrl;
class wxStaticText;
class wxButton;
class wxScrolledWindow;
class weatherfiles_pi;

// Area-first multi-slice download dialog (mirrors the main website's flow).
//
//   1. User draws or types an N/S/W/E area (default: current chart view).
//   2. User picks models from three category sections - Atmosphere (cap 3),
//      Waves (cap 1), Currents (cap 1). Models the tier can't slice are
//      disabled; models whose grid doesn't intersect the area are greyed out
//      with an "outside coverage" note (still visible so users discover the
//      catalogue).
//   3. "Download N slices" opens WfDownloadProgress in queue mode, which
//      downloads them serially and auto-opens up to 3 files in grib_pi (first
//      atmosphere + the wave + the current).
//
// The dialog is modeless so the user can still interact with the chart (e.g.
// click "Draw on chart" and rubber-band a new area without losing dialog
// state); WfDownloadProgress is then run modally over it. The owner plugin
// holds the pointer and clears it in the dialog destructor.
class WfMultiSliceDialog : public wxDialog {
 public:
  WfMultiSliceDialog(wxWindow* parent, const wxString& token,
                     const WfBBox& default_box, weatherfiles_pi* owner);
  ~WfMultiSliceDialog() override;

  // Plugin calls this on a rubber-band area release.
  void SetBox(const WfBBox& box);

 private:
  struct ModelRow {
    WfModel model;
    wxString category;     // "atmosphere" | "wave" | "current"
    wxCheckBox* check = nullptr;
  };

  void Load();   // sync: ValidateToken -> FetchModels -> Populate
  void Populate();
  void BuildModelSection(wxScrolledWindow* host, wxSizer* host_sizer,
                         const wxString& title, const wxString& category,
                         int cap);
  void OnUseView(wxCommandEvent& evt);
  void OnDraw(wxCommandEvent& evt);
  void OnDownload(wxCommandEvent& evt);
  void OnCheck(wxCommandEvent& evt);
  void OnClose(wxCloseEvent& evt);
  void UpdateSelectionState();   // enforces caps + refreshes summary text
  void RecomputeBounds();        // updates time window/step ranges
  std::vector<ModelRow*> SelectedRows(const wxString& category);
  bool BBoxIntersects(const WfModel& m, double s, double n, double w,
                      double e) const;
  double FieldVal(wxTextCtrl* c, double fallback) const;

  wxString m_token;
  WfApi m_api;
  weatherfiles_pi* m_owner;
  WfBBox m_default_box;
  std::vector<wxString> m_allowed;  // tier-allowed model ids
  std::vector<ModelRow> m_rows;

  wxTextCtrl* m_north = nullptr;
  wxTextCtrl* m_south = nullptr;
  wxTextCtrl* m_west = nullptr;
  wxTextCtrl* m_east = nullptr;
  wxSpinCtrl* m_window = nullptr;
  wxSpinCtrl* m_step = nullptr;
  wxStaticText* m_status = nullptr;
  wxButton* m_download = nullptr;
  wxScrolledWindow* m_body = nullptr;
  wxSizer* m_atmoSizer = nullptr;
  wxSizer* m_waveSizer = nullptr;
  wxSizer* m_currSizer = nullptr;
};

#endif  // WF_MULTI_SLICE_DIALOG_H
