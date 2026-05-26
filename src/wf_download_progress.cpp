// WeatherFiles threaded download progress dialog. See wf_download_progress.h.

#include "wf_download_progress.h"

#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "wf_api.h"

enum { ID_WF_DLCANCEL = wxID_HIGHEST + 30, ID_WF_DLTIMER };

WfDownloadProgress::WfDownloadProgress(wxWindow* parent, const wxString& token,
                                       const wxString& query,
                                       const wxString& out_path)
    : wxDialog(parent, wxID_ANY, _("Downloading GRIB"), wxDefaultPosition,
               wxDefaultSize, wxCAPTION),  // no close box - Cancel only
      m_timer(this, ID_WF_DLTIMER) {
  auto* top = new wxBoxSizer(wxVERTICAL);
  m_status = new wxStaticText(this, wxID_ANY, _("Preparing slice..."),
                              wxDefaultPosition, wxSize(360, -1));
  top->Add(m_status, 0, wxALL, 12);
  m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(360, 16));
  m_gauge->Pulse();  // indeterminate until bytes arrive
  top->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->AddStretchSpacer();
  m_cancelBtn = new wxButton(this, ID_WF_DLCANCEL, _("Cancel"));
  row->Add(m_cancelBtn, 0);
  top->Add(row, 0, wxEXPAND | wxALL, 12);
  SetSizerAndFit(top);

  Bind(wxEVT_TIMER, &WfDownloadProgress::OnTimer, this, ID_WF_DLTIMER);
  Bind(wxEVT_BUTTON, &WfDownloadProgress::OnCancel, this, ID_WF_DLCANCEL);
  Bind(wxEVT_CLOSE_WINDOW, &WfDownloadProgress::OnClose, this);

  m_watch.Start();
  m_timer.Start(200);

  // Worker thread: synchronous download, updating atomics only - no GUI access.
  m_worker = std::thread([this, token, query, out_path]() {
    WfApi api(token);
    api.DownloadGrib(
        query, out_path,
        [this](bool ok, const wxString& err) {
          m_error = err;
          m_ok.store(ok);
          m_done.store(true);  // last: publishes m_error/m_ok to the GUI thread
        },
        [this](long bytes, long total) {
          m_bytes.store(bytes);
          m_total.store(total);
          return !m_cancel.load();
        });
  });
}

WfDownloadProgress::~WfDownloadProgress() {
  m_cancel.store(true);
  if (m_worker.joinable()) m_worker.join();
}

void WfDownloadProgress::OnTimer(wxTimerEvent&) {
  if (m_done.load()) { Finish(); return; }

  const long bytes = m_bytes.load();
  const long total = m_total.load();
  if (bytes <= 0) {
    m_status->SetLabel(
        wxString::Format(_("Preparing slice... %lds"), m_watch.Time() / 1000));
    m_gauge->Pulse();
  } else if (total > 0) {
    const int pct = static_cast<int>((static_cast<long long>(bytes) * 100) / total);
    m_status->SetLabel(wxString::Format(_("Downloading... %ld / %ld KB (%d%%)"),
                                        bytes / 1024, total / 1024, pct));
    m_gauge->SetValue(pct);
  } else {
    m_status->SetLabel(wxString::Format(_("Downloading... %ld KB"), bytes / 1024));
    m_gauge->Pulse();
  }
}

void WfDownloadProgress::OnCancel(wxCommandEvent&) {
  m_cancel.store(true);
  m_cancelBtn->Disable();
  m_status->SetLabel(_("Cancelling..."));
}

void WfDownloadProgress::OnClose(wxCloseEvent& evt) {
  if (!m_done.load()) {  // don't tear down while the worker holds `this`
    m_cancel.store(true);
    evt.Veto();
    return;
  }
  evt.Skip();
}

void WfDownloadProgress::Finish() {
  if (m_finished) return;
  m_finished = true;
  m_timer.Stop();
  if (m_worker.joinable()) m_worker.join();
  EndModal(m_ok.load() ? wxID_OK : wxID_CANCEL);
}
