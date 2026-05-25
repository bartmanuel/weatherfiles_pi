#ifndef WF_MODELS_PANEL_H
#define WF_MODELS_PANEL_H

#include <wx/dialog.h>
#include <wx/string.h>

#include <vector>

#include "wf_api.h"

class wxListCtrl;
class wxStaticText;
class wxButton;

// WeatherFiles model browser: lists the catalogue from GET /v1/models with
// region/params/horizon, and greys out models the current tier can't slice
// (from /v1/auth/me allowed_models). Opened from the toolbar button.
class WfModelsPanel : public wxDialog {
 public:
  WfModelsPanel(wxWindow* parent, const wxString& token);

 private:
  void Load();                       // validate token -> fetch models -> render
  void OnRefresh(wxCommandEvent& evt);
  void Populate(const std::vector<WfModel>& models);
  void SetStatus(const wxString& text, bool ok);

  wxListCtrl* m_list = nullptr;
  wxStaticText* m_status = nullptr;
  wxButton* m_refresh = nullptr;
  WfApi m_api;
  std::vector<wxString> m_allowed;   // model ids this tier may slice
};

#endif  // WF_MODELS_PANEL_H
