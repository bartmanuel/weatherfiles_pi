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
class wxStaticBox;
class wxGauge;
class wxButton;
class wxScrolledWindow;
class wxTextCtrl;
class wxCheckBox;
class wxChoice;
class wxSizer;
class wxPanel;

// One model in a queue download: the prebuilt /v1/grib query string, the
// file to write, and the display info shown in the progress + summary
// lists. `category` ("atmosphere"|"wave"|"current") drives the post-download
// per-category picker (radio buttons if multiple in a category, single
// checkbox if just one).
//
// The structured fields (model_id, params, bbox, time_window_h, time_step_h,
// spatial_factor) carry the original request shape so the post-download
// "Save as set" flow can POST /v1/slices without parsing the query string
// back out. They're populated for area-wizard downloads only; the slice
// re-download path (start screen) leaves them empty and disables saving.
struct WfDownloadJob {
  wxString label;
  wxString category;
  wxString query;
  wxString out_path;

  // For "Save as set" (optional; empty for slice-replay jobs).
  wxString model_id;
  std::vector<wxString> params;
  wxString bbox;                  // "w,e,s,n" - matches /v1/slices schema
  int time_window_h = 0;
  int time_step_h = 0;
  int spatial_factor = 0;         // 0 == omit (server default = 1)
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
  //
  // `existing_tags` is the user's currently-known tag set (for collision
  // detection in the "Save as set" input). `can_save_as_set` toggles the
  // whole Save section - false for slice-replay flows (start screen), true
  // for area-wizard downloads where the result is genuinely new.
  WfDownloadProgress(wxWindow* parent, const wxString& token,
                     std::vector<WfDownloadJob> jobs,
                     std::vector<wxString> existing_tags = {},
                     bool can_save_as_set = false);
  ~WfDownloadProgress();

  const wxString& Error() const { return m_error; }
  bool Cancelled() const { return m_cancel.load(); }
  const std::vector<WfDownloadResult>& Results() const { return m_results; }

 private:
  void BuildUi(bool queue_mode);
  void OnTimer(wxTimerEvent& evt);
  void OnCancel(wxCommandEvent& evt);   // Cancel during download / Close after
  void OnClose(wxCloseEvent& evt);
  void OnOpenFolder(wxCommandEvent& evt);
  void OnSetNameChanged(wxCommandEvent& evt);
  void OnSavePressed(wxCommandEvent& evt);   // explicit Save button
  void OnLoadSelected(wxCommandEvent& evt);
  void StartWorker(const wxString& token);
  void RenderProgress();         // per-tick progress text/gauge
  void RenderQueueComplete();    // swap to summary view
  void BuildSavePanel(wxSizer* host);
  void BuildLoaderPanel(wxSizer* host);
  void ValidateSetName();        // recolour input + toggle Close label
  void RefreshLoadButtonEnabled();
  bool PerformSaveSlices();      // POSTs /v1/slices per job - sync
  void LoadSelectedInViewer();   // concat + GRIB_APPLY_JSON_CONFIG
  void Finish();                 // join worker + EndModal based on result

  wxStaticText* m_status = nullptr;
  wxGauge* m_gauge = nullptr;
  wxButton* m_cancelBtn = nullptr;        // Cancel during, Close after
  wxButton* m_openFolderBtn = nullptr;
  wxScrolledWindow* m_completedPane = nullptr;
  wxStaticBox* m_listBox = nullptr;       // labeled box around the list
  wxStaticText* m_savedToText = nullptr;  // bottom-left of the list box
  wxTimer m_timer;
  wxStopWatch m_watch;

  // Top-level vertical sizer + insertion host for post-completion content.
  // RenderQueueComplete adds the save + loader panels to m_postCompleteHost
  // (an empty vertical sizer reserved in BuildUi between the file list and
  // the footer button row).
  wxSizer* m_topSizer = nullptr;
  wxSizer* m_postCompleteHost = nullptr;

  // Post-completion UI - built once in RenderQueueComplete().
  wxPanel* m_savePanel = nullptr;
  wxTextCtrl* m_saveInput = nullptr;
  wxButton* m_saveBtn = nullptr;             // explicit Save action
  wxStaticText* m_saveHint = nullptr;        // validation feedback
  wxPanel* m_loaderPanel = nullptr;
  wxButton* m_loadBtn = nullptr;
  wxStaticText* m_loaderStatus = nullptr;    // "Loaded into GRIB viewer." etc.

  // Per-category picker state, built from m_results after completion.
  // results.size() == 1 -> single default-ON checkbox.
  // results.size()  > 1 -> wxChoice dropdown; item 0 is "--- none ---" (the
  //                       default), items 1..N map to results[sel-1].
  struct LoaderEntry {
    wxString category;
    std::vector<const WfDownloadResult*> results;
    wxCheckBox* check = nullptr;
    wxChoice* choice = nullptr;
  };
  std::vector<LoaderEntry> m_loaderEntries;

  // Tag collision check + dynamic Close label.
  std::vector<wxString> m_existingTags;     // normalised: lower + trimmed
  bool m_canSaveAsSet = false;
  wxString m_currentTag;                    // cleaned (lower + trimmed)
  bool m_currentTagValid = false;

  // Stashed API token - the constructor receives it for the download worker;
  // PerformSaveSlices needs it again to POST /v1/slices for each job.
  wxString m_token;

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
