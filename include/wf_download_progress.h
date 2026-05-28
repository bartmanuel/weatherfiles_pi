#ifndef WF_DOWNLOAD_PROGRESS_H
#define WF_DOWNLOAD_PROGRESS_H

#include <wx/dialog.h>
#include <wx/stopwatch.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <atomic>
#include <thread>
#include <vector>

class wxStaticText;
class wxGauge;
class wxButton;
class wxScrolledWindow;

// One model in a queue download: the prebuilt /v1/grib query string, the file
// to write, and the display info shown in the progress + summary lists.
// `category` ("atmosphere"|"wave"|"current") determines which file(s) get
// auto-handed to grib_pi at the end (the first atmosphere + the wave + the
// current that succeeded).
struct WfDownloadJob {
  wxString label;
  wxString category;
  wxString query;
  wxString out_path;
};

struct WfDownloadResult {
  WfDownloadJob job;
  bool ok = false;
  long bytes = 0;
  wxString error;
};

// Modal download dialog with live progress. Supports two modes:
//
//   Single-job   - one query/out_path; legacy WfDownloadDialog uses this.
//   Queue (N)    - a vector of WfDownloadJob; runs them serially on the worker
//                  (one libcurl/WinHTTP call at a time, clean progress
//                  accounting, no double rate-limit hits). On completion the
//                  dialog stays open showing a per-job summary list with
//                  "Open folder" + "Re-open in GRIB viewer" buttons; the GRIB
//                  handoff (currents -> waves -> first atmosphere; the order
//                  makes atmospheric the active file whether grib_pi stacks
//                  files or replaces last-wins) fires once automatically.
//
// The download(s) run on a worker thread that only touches the atomics below
// (never the GUI); a timer on the GUI thread renders the state. ShowModal()
// returns wxID_OK if any job succeeded (queue mode) or the single download
// succeeded; otherwise wxID_CANCEL. Error()/Cancelled() describe the outcome.
class WfDownloadProgress : public wxDialog {
 public:
  // Legacy single-job constructor (kept for WfDownloadDialog).
  WfDownloadProgress(wxWindow* parent, const wxString& token,
                     const wxString& query, const wxString& out_path);

  // Queue constructor: N jobs run serially in arrival order.
  WfDownloadProgress(wxWindow* parent, const wxString& token,
                     std::vector<WfDownloadJob> jobs);
  ~WfDownloadProgress();

  const wxString& Error() const { return m_error; }
  bool Cancelled() const { return m_cancel.load(); }
  const std::vector<WfDownloadResult>& Results() const { return m_results; }

 private:
  void BuildUi(bool queue_mode);
  void OnTimer(wxTimerEvent& evt);
  void OnCancel(wxCommandEvent& evt);
  void OnClose(wxCloseEvent& evt);
  void OnOpenFolder(wxCommandEvent& evt);
  void OnReopenViewer(wxCommandEvent& evt);
  void StartWorker(const wxString& token);
  void RenderProgress();         // per-tick progress text/gauge
  void RenderQueueComplete();    // swap to summary view
  void HandoffToGribViewer();    // up to 3 SendPluginMessage calls
  void Finish();                 // join worker + EndModal based on result

  wxStaticText* m_status = nullptr;
  wxGauge* m_gauge = nullptr;
  wxButton* m_cancelBtn = nullptr;
  wxButton* m_openFolderBtn = nullptr;  // queue mode, post-completion
  wxButton* m_reopenViewerBtn = nullptr;
  wxScrolledWindow* m_completedPane = nullptr;  // queue mode
  wxTimer m_timer;
  wxStopWatch m_watch;

  std::thread m_worker;
  std::atomic<long> m_bytes{0};
  std::atomic<long> m_total{0};
  std::atomic<int> m_jobIndex{0};   // queue mode: which job is current (0..N-1)
  std::atomic<bool> m_done{false};
  std::atomic<bool> m_ok{false};
  std::atomic<bool> m_cancel{false};

  // Queue mode: jobs are read-only by the worker; results are written by the
  // worker (one per job) and published via m_jobIndex/m_done before the GUI
  // reads them. m_completedSeen tracks how many we've already painted into
  // m_completedPane (incremental so the list grows live).
  std::vector<WfDownloadJob> m_jobs;
  std::vector<WfDownloadResult> m_results;
  int m_completedSeen = 0;
  bool m_queueMode = false;
  bool m_handoffDone = false;

  wxString m_error;          // written by worker before m_done; read after join
  bool m_finished = false;   // Finish() runs once
};

#endif  // WF_DOWNLOAD_PROGRESS_H
