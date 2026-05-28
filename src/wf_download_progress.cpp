// WeatherFiles threaded download progress dialog. See wf_download_progress.h.

#include "wf_download_progress.h"

#include <wx/button.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/jsonval.h>
#include <wx/jsonwriter.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>  // wxLaunchDefaultApplication

#include "ocpn_plugin.h"  // SendPluginMessage
#include "wf_api.h"

enum {
  ID_WF_DLCANCEL = wxID_HIGHEST + 30,
  ID_WF_DLTIMER,
  ID_WF_DLOPEN_FOLDER,
  ID_WF_DLREOPEN_VIEWER,
};

static wxString FmtMb(long bytes) {
  const double mb = bytes / (1024.0 * 1024.0);
  return wxString::Format("%.1f MB", mb);
}

WfDownloadProgress::WfDownloadProgress(wxWindow* parent, const wxString& token,
                                       const wxString& query,
                                       const wxString& out_path)
    : wxDialog(parent, wxID_ANY, _("Downloading GRIB"), wxDefaultPosition,
               wxDefaultSize, wxCAPTION),  // no close box - Cancel only
      m_timer(this, ID_WF_DLTIMER) {
  WfDownloadJob job;
  job.query = query;
  job.out_path = out_path;
  m_jobs.push_back(job);
  m_results.resize(1);
  m_queueMode = false;
  BuildUi(false);
  StartWorker(token);
}

WfDownloadProgress::WfDownloadProgress(wxWindow* parent, const wxString& token,
                                       std::vector<WfDownloadJob> jobs)
    : wxDialog(parent, wxID_ANY, _("Downloading GRIBs"), wxDefaultPosition,
               wxDefaultSize, wxCAPTION),
      m_timer(this, ID_WF_DLTIMER) {
  m_jobs = std::move(jobs);
  m_results.resize(m_jobs.size());
  m_queueMode = !m_jobs.empty();
  BuildUi(m_queueMode);
  StartWorker(token);
}

void WfDownloadProgress::BuildUi(bool queue_mode) {
  auto* top = new wxBoxSizer(wxVERTICAL);

  m_status = new wxStaticText(this, wxID_ANY, _("Preparing slice..."),
                              wxDefaultPosition, wxSize(440, -1));
  top->Add(m_status, 0, wxALL, 12);

  m_gauge = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(440, 16));
  m_gauge->Pulse();
  top->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

  if (queue_mode) {
    auto* header = new wxStaticText(this, wxID_ANY, _("Completed:"));
    top->Add(header, 0, wxLEFT | wxTOP, 12);
    m_completedPane = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                           wxSize(440, 120));
    m_completedPane->SetSizer(new wxBoxSizer(wxVERTICAL));
    m_completedPane->SetScrollRate(0, 10);
    top->Add(m_completedPane, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  if (queue_mode) {
    m_openFolderBtn = new wxButton(this, ID_WF_DLOPEN_FOLDER, _("Open folder"));
    m_openFolderBtn->Hide();
    row->Add(m_openFolderBtn, 0, wxRIGHT, 8);
    m_reopenViewerBtn =
        new wxButton(this, ID_WF_DLREOPEN_VIEWER, _("Re-open in GRIB viewer"));
    m_reopenViewerBtn->Hide();
    row->Add(m_reopenViewerBtn, 0, wxRIGHT, 8);
  }
  row->AddStretchSpacer();
  m_cancelBtn = new wxButton(this, ID_WF_DLCANCEL, _("Cancel"));
  row->Add(m_cancelBtn, 0);
  top->Add(row, 0, wxEXPAND | wxALL, 12);

  SetSizerAndFit(top);

  Bind(wxEVT_TIMER, &WfDownloadProgress::OnTimer, this, ID_WF_DLTIMER);
  Bind(wxEVT_BUTTON, &WfDownloadProgress::OnCancel, this, ID_WF_DLCANCEL);
  Bind(wxEVT_CLOSE_WINDOW, &WfDownloadProgress::OnClose, this);
  if (queue_mode) {
    Bind(wxEVT_BUTTON, &WfDownloadProgress::OnOpenFolder, this,
         ID_WF_DLOPEN_FOLDER);
    Bind(wxEVT_BUTTON, &WfDownloadProgress::OnReopenViewer, this,
         ID_WF_DLREOPEN_VIEWER);
  }

  m_watch.Start();
  m_timer.Start(200);
}

void WfDownloadProgress::StartWorker(const wxString& token) {
  // Worker thread: runs each job synchronously, updating atomics only - never
  // touches the GUI. After each job it writes m_results[i] and *then* advances
  // m_jobIndex (release-acquire ordering via seq_cst stores), so the GUI can
  // safely read m_results[0..m_jobIndex-1].
  m_worker = std::thread([this, token]() {
    WfApi api(token);
    for (size_t i = 0; i < m_jobs.size(); ++i) {
      if (m_cancel.load()) break;
      m_jobIndex.store(static_cast<int>(i));
      m_bytes.store(0);
      m_total.store(0);

      bool job_ok = false;
      wxString job_err;
      long final_bytes = 0;
      api.DownloadGrib(
          m_jobs[i].query, m_jobs[i].out_path,
          [&job_ok, &job_err](bool ok, const wxString& err) {
            job_ok = ok;
            job_err = err;
          },
          [this, &final_bytes](long b, long t) {
            m_bytes.store(b);
            m_total.store(t);
            final_bytes = b;
            return !m_cancel.load();
          });

      m_results[i].job = m_jobs[i];
      m_results[i].ok = job_ok;
      m_results[i].bytes = final_bytes;
      m_results[i].error = job_err;
      if (job_ok) m_ok.store(true);
      // Publishes m_results[i] to the GUI thread (acquire on m_jobIndex pairs
      // with this seq_cst store).
      m_jobIndex.store(static_cast<int>(i + 1));
    }
    if (!m_ok.load() && !m_error.IsEmpty()) {
      // (Single-job legacy path - on failure the per-job error is also the
      // dialog-level error so WfDownloadDialog can show it.)
    } else if (!m_results.empty() && !m_results[0].ok) {
      m_error = m_results[0].error;
    }
    m_done.store(true);
  });
}

WfDownloadProgress::~WfDownloadProgress() {
  m_cancel.store(true);
  if (m_worker.joinable()) m_worker.join();
}

void WfDownloadProgress::RenderProgress() {
  const int idx = m_jobIndex.load();
  const long bytes = m_bytes.load();
  const long total = m_total.load();

  wxString prefix;
  if (m_queueMode && static_cast<size_t>(idx) < m_jobs.size()) {
    prefix = wxString::Format(_("Downloading %d of %zu: %s\n"), idx + 1,
                              m_jobs.size(), m_jobs[idx].label);
  }

  if (bytes <= 0) {
    m_status->SetLabel(prefix + wxString::Format(_("Preparing slice... %lds"),
                                                 m_watch.Time() / 1000));
    m_gauge->Pulse();
  } else if (total > 0) {
    const int pct =
        static_cast<int>((static_cast<long long>(bytes) * 100) / total);
    m_status->SetLabel(prefix + wxString::Format(_("%ld / %ld KB (%d%%)"),
                                                 bytes / 1024, total / 1024, pct));
    m_gauge->SetValue(pct);
  } else {
    m_status->SetLabel(prefix +
                       wxString::Format(_("%ld KB"), bytes / 1024));
    m_gauge->Pulse();
  }

  if (m_queueMode) {
    // Paint any newly-completed entries into the running list.
    const int complete = m_jobIndex.load();
    auto* sizer = m_completedPane->GetSizer();
    for (int i = m_completedSeen; i < complete; ++i) {
      const auto& r = m_results[i];
      wxString line;
      if (r.ok) {
        line = wxString::Format("  [OK]  %s  -  %s", r.job.label,
                                FmtMb(r.bytes));
      } else {
        line = wxString::Format("  [X]   %s  -  %s", r.job.label,
                                r.error.IsEmpty() ? _("failed") : r.error);
      }
      auto* txt = new wxStaticText(m_completedPane, wxID_ANY, line);
      if (!r.ok)
        txt->SetForegroundColour(wxColour(0xc0, 0x40, 0x40));
      sizer->Add(txt, 0, wxLEFT | wxTOP, 2);
    }
    if (complete > m_completedSeen) {
      m_completedSeen = complete;
      m_completedPane->FitInside();
      m_completedPane->Layout();
    }
  }
}

void WfDownloadProgress::OnTimer(wxTimerEvent&) {
  if (m_done.load()) {
    if (m_queueMode) {
      RenderQueueComplete();  // stays open; user dismisses via Close
    } else {
      Finish();
    }
    return;
  }
  RenderProgress();
}

void WfDownloadProgress::RenderQueueComplete() {
  if (m_handoffDone) return;  // run once
  m_handoffDone = true;
  m_timer.Stop();
  if (m_worker.joinable()) m_worker.join();

  // Make sure any final result is in the list (timer may not have run since
  // the last m_jobIndex bump).
  RenderProgress();

  int ok_count = 0;
  long total_bytes = 0;
  for (const auto& r : m_results) {
    if (r.ok) {
      ++ok_count;
      total_bytes += r.bytes;
    }
  }
  const int n = static_cast<int>(m_results.size());

  // Drop any successful file's parent dir into the header so the user knows
  // exactly where their files are.
  wxString parent_dir;
  for (const auto& r : m_results) {
    if (r.ok) {
      parent_dir = wxFileName(r.job.out_path).GetPath();
      break;
    }
  }

  wxString header;
  if (m_cancel.load()) {
    header =
        wxString::Format(_("Cancelled - %d of %d downloaded (%s)"), ok_count, n,
                         FmtMb(total_bytes));
  } else if (ok_count == n) {
    header = wxString::Format(_("Done - %d of %d downloaded (%s)"), ok_count, n,
                              FmtMb(total_bytes));
  } else {
    header = wxString::Format(_("Done - %d of %d succeeded (%s)"), ok_count, n,
                              FmtMb(total_bytes));
  }
  if (!parent_dir.IsEmpty()) {
    header += "\n";
    header += _("Files saved to: ");
    header += parent_dir;
  }
  m_status->SetLabel(header);
  m_gauge->SetValue(100);
  m_gauge->Hide();

  m_cancelBtn->SetLabel(_("Close"));
  if (m_openFolderBtn) {
    m_openFolderBtn->Show(!parent_dir.IsEmpty());
    m_openFolderBtn->SetClientObject(new wxStringClientData(parent_dir));
  }
  if (m_reopenViewerBtn) m_reopenViewerBtn->Show(ok_count > 0);

  Layout();
  Fit();

  HandoffToGribViewer();
}

void WfDownloadProgress::HandoffToGribViewer() {
  // Pick targets: the first atmosphere, the wave, the current that succeeded.
  // Send order: current -> wave -> atmosphere. If grib_pi stacks files, all
  // three load and the last-sent (atmosphere) is the active one. If it
  // replaces (last-wins), only the atmosphere remains - the "open the first
  // atmospheric, fall back if multi-load doesn't work" semantic.
  const WfDownloadResult* atmo = nullptr;
  const WfDownloadResult* wave = nullptr;
  const WfDownloadResult* curr = nullptr;
  for (const auto& r : m_results) {
    if (!r.ok) continue;
    if (!atmo && r.job.category == "atmosphere") atmo = &r;
    else if (!wave && r.job.category == "wave") wave = &r;
    else if (!curr && r.job.category == "current") curr = &r;
  }
  const WfDownloadResult* order[3] = {curr, wave, atmo};
  for (const auto* r : order) {
    if (!r) continue;
    wxJSONValue cfg;
    cfg[_T("grib_file")] = r->job.out_path;
    wxJSONWriter writer(wxJSONWRITER_NONE);
    wxString body;
    writer.Write(cfg, body);
    SendPluginMessage(_T("GRIB_APPLY_JSON_CONFIG"), body);
  }
}

void WfDownloadProgress::OnOpenFolder(wxCommandEvent&) {
  if (!m_openFolderBtn) return;
  auto* data =
      static_cast<wxStringClientData*>(m_openFolderBtn->GetClientObject());
  if (data) wxLaunchDefaultApplication(data->GetData());
}

void WfDownloadProgress::OnReopenViewer(wxCommandEvent&) {
  HandoffToGribViewer();
}

void WfDownloadProgress::OnCancel(wxCommandEvent&) {
  if (m_done.load() && m_queueMode) {
    // In summary state: button has been relabeled "Close"; just dismiss.
    EndModal(m_ok.load() ? wxID_OK : wxID_CANCEL);
    return;
  }
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
