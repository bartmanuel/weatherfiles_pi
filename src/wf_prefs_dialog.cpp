// WeatherFiles preferences dialog. See wf_prefs_dialog.h.
//
// NOTE (build): wxWidgets 3.2 GUI code not compiled in this environment; the
// first CI build will confirm widget/API details.

#include "wf_prefs_dialog.h"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>  // wxBusyCursor

enum { ID_WF_VALIDATE = wxID_HIGHEST + 1 };

WfPrefsDialog::WfPrefsDialog(wxWindow* parent, const wxString& token)
    : wxDialog(parent, wxID_ANY, _("WeatherFiles"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
      m_api(token) {
  auto* top = new wxBoxSizer(wxVERTICAL);

  top->Add(new wxStaticText(
               this, wxID_ANY,
               _("Paste your WeatherFiles personal access token (wf_pat_...).\n"
                 "Create one at weatherfiles.com/account/tokens.")),
           0, wxALL, 10);

  m_token_ctrl = new wxTextCtrl(this, wxID_ANY, token, wxDefaultPosition,
                                wxSize(380, -1));
  top->Add(m_token_ctrl, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  m_validate_btn = new wxButton(this, ID_WF_VALIDATE, _("Validate"));
  row->Add(m_validate_btn, 0, wxRIGHT, 8);
  m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
  row->Add(m_status, 1, wxALIGN_CENTER_VERTICAL);
  top->Add(row, 0, wxEXPAND | wxALL, 10);

  top->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 10);

  SetSizerAndFit(top);

  Bind(wxEVT_BUTTON, &WfPrefsDialog::OnValidate, this, ID_WF_VALIDATE);
}

wxString WfPrefsDialog::GetToken() const {
  if (!m_token_ctrl) return wxEmptyString;
  wxString t = m_token_ctrl->GetValue();
  t.Trim(true).Trim(false);  // strip trailing then leading whitespace
  return t;
}

void WfPrefsDialog::SetStatus(const wxString& text, bool ok) {
  if (!m_status) return;
  // Theme-safe colours: *wxBLACK was unreadable on macOS dark-mode's grey
  // dialog background. These greens/reds read on both light and dark.
  m_status->SetForegroundColour(ok ? wxColour(0x1a, 0x9e, 0x1a)
                                   : wxColour(0xe0, 0x40, 0x40));
  m_status->SetLabel(text);
  Layout();
  m_status->Update();  // immediate repaint (HTTP calls block the GUI thread)
}

void WfPrefsDialog::OnValidate(wxCommandEvent&) {
  const wxString tok = GetToken();
  if (tok.IsEmpty()) {
    SetStatus(_("Enter a token first."), false);
    return;
  }
  m_api.SetToken(tok);
  m_validate_btn->Disable();
  SetStatus(_("Validating..."), true);
  wxBusyCursor busy;  // the call below blocks the GUI thread
  m_api.ValidateToken(
      [this](bool ok, const WfAccount& acct, const wxString& err) {
        m_validate_btn->Enable();
        if (!ok) {
          SetStatus(_("Invalid: ") + err, false);
          return;
        }
        wxString quota =
            acct.daily_download_limit < 0
                ? wxString(_("unlimited downloads"))
                : wxString::Format("%d/%d downloads today",
                                   acct.daily_downloads_used_today,
                                   acct.daily_download_limit);
        SetStatus(wxString::Format("Token valid - %s (%s), %s",
                                   acct.email, acct.tier, quota),
                  true);
      });
}
