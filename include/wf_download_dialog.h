#ifndef WF_DOWNLOAD_DIALOG_H
#define WF_DOWNLOAD_DIALOG_H

#include <wx/dialog.h>
#include <wx/string.h>

#include "wf_api.h"

class wxTextCtrl;
class wxSpinCtrl;
class wxCheckListBox;
class wxStaticText;
class wxButton;

// One-shot GRIB download for a single model: edit the area (N/S/W/E, defaulting
// to the current view clamped to the model domain, else the full domain), pick
// params + forecast window/step, then GET /v1/grib, save the file, and hand it
// to OpenCPN's GRIB plugin (GRIB_APPLY_JSON_CONFIG).
class WfDownloadDialog : public wxDialog {
 public:
  WfDownloadDialog(wxWindow* parent, const WfModel& model,
                   const WfBBox& default_box, const wxString& token);

 private:
  void OnDownload(wxCommandEvent& evt);
  void SetStatus(const wxString& text, bool ok);
  double FieldVal(wxTextCtrl* c, double fallback) const;

  WfModel m_model;
  WfApi m_api;
  wxTextCtrl* m_north = nullptr;
  wxTextCtrl* m_south = nullptr;
  wxTextCtrl* m_west = nullptr;
  wxTextCtrl* m_east = nullptr;
  wxSpinCtrl* m_window = nullptr;
  wxSpinCtrl* m_step = nullptr;
  wxCheckListBox* m_params = nullptr;
  wxStaticText* m_status = nullptr;
  wxButton* m_download = nullptr;
};

#endif  // WF_DOWNLOAD_DIALOG_H
