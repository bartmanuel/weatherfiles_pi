#ifndef WF_DOWNLOAD_PROGRESS_H
#define WF_DOWNLOAD_PROGRESS_H

#include <wx/dialog.h>
#include <wx/stopwatch.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <atomic>
#include <thread>

class wxStaticText;
class wxGauge;
class wxButton;

// Modal download dialog with live progress. The GET /v1/grib download runs on a
// worker thread (which only touches the atomics below - never the GUI); a timer
// on the GUI thread renders the state: a "Preparing slice... Ns" phase (server
// slicer prep, before any bytes arrive) then a KB / total-KB counter once the
// download streams. Cancel aborts. ShowModal() returns wxID_OK on success and
// wxID_CANCEL on failure/cancel; Error()/Cancelled() describe the outcome.
class WfDownloadProgress : public wxDialog {
 public:
  WfDownloadProgress(wxWindow* parent, const wxString& token,
                     const wxString& query, const wxString& out_path);
  ~WfDownloadProgress();

  const wxString& Error() const { return m_error; }
  bool Cancelled() const { return m_cancel.load(); }

 private:
  void OnTimer(wxTimerEvent& evt);
  void OnCancel(wxCommandEvent& evt);
  void OnClose(wxCloseEvent& evt);
  void Finish();  // join the worker + EndModal based on the result

  wxStaticText* m_status = nullptr;
  wxGauge* m_gauge = nullptr;
  wxButton* m_cancelBtn = nullptr;
  wxTimer m_timer;
  wxStopWatch m_watch;

  std::thread m_worker;
  std::atomic<long> m_bytes{0};
  std::atomic<long> m_total{0};
  std::atomic<bool> m_done{false};
  std::atomic<bool> m_ok{false};
  std::atomic<bool> m_cancel{false};
  wxString m_error;          // written by worker before m_done; read after join
  bool m_finished = false;   // Finish() runs once
};

#endif  // WF_DOWNLOAD_PROGRESS_H
