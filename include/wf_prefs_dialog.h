// SPDX-FileCopyrightText: 2026 Bart Manuel
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WF_PREFS_DIALOG_H
#define WF_PREFS_DIALOG_H

#include <wx/dialog.h>
#include <wx/string.h>

#include "wf_api.h"

class wxTextCtrl;
class wxStaticText;
class wxButton;

// WeatherFiles preferences: paste a personal access token (wf_pat_...), click
// Validate to check it against GET /v1/auth/me (shows tier + daily quota), and
// OK to keep it. The plugin reads GetToken() after a wxID_OK close and persists
// it to the OpenCPN config.
class WfPrefsDialog : public wxDialog {
 public:
  WfPrefsDialog(wxWindow* parent, const wxString& token);

  // The (trimmed) token currently in the field; read by the caller on wxID_OK.
  wxString GetToken() const;

 private:
  void OnValidate(wxCommandEvent& evt);
  void SetStatus(const wxString& text, bool ok);

  wxTextCtrl* m_token_ctrl = nullptr;
  wxStaticText* m_status = nullptr;
  wxButton* m_validate_btn = nullptr;
  // Owned here so it outlives the in-flight request for the dialog's lifetime
  // (the dialog is modal, so its event loop pumps the async callback).
  WfApi m_api;
};

#endif  // WF_PREFS_DIALOG_H
