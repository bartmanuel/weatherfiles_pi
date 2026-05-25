#ifndef WF_MODELS_PANEL_H
#define WF_MODELS_PANEL_H

#include <wx/dialog.h>
#include <wx/string.h>

#include <vector>

#include "wf_api.h"

class wxListCtrl;
class wxListEvent;
class wxStaticText;
class wxButton;

// WeatherFiles model browser: lists the catalogue from GET /v1/models with
// region/params/horizon, and greys out models the current tier can't slice
// (from /v1/auth/me allowed_models). Selecting an allowed model and clicking
// Download (or double-clicking it) opens the one-shot GRIB download dialog.
// Opened from the toolbar button; `default_box` is the current chart view.
class WfModelsPanel : public wxDialog {
 public:
  WfModelsPanel(wxWindow* parent, const wxString& token,
                const WfBBox& default_box);

 private:
  void Load();                       // validate token -> fetch models -> render
  void OnRefresh(wxCommandEvent& evt);
  void OnActivate(wxListEvent& evt);  // double-click a row
  void OnDownload(wxCommandEvent& evt);
  void OpenDownloadForSelection();
  void Populate(const std::vector<WfModel>& models);
  void SetStatus(const wxString& text, bool ok);

  wxListCtrl* m_list = nullptr;
  wxStaticText* m_status = nullptr;
  wxButton* m_refresh = nullptr;
  wxButton* m_download = nullptr;
  WfApi m_api;
  std::vector<wxString> m_allowed;   // model ids this tier may slice
  std::vector<WfModel> m_models;     // current catalogue (row index -> model)
  WfBBox m_default_box;              // current chart view, for download default
};

#endif  // WF_MODELS_PANEL_H
