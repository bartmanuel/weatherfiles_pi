// WeatherFiles threaded download progress dialog. See wf_download_progress.h.

#include "wf_download_progress.h"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/filename.h>
#include <wx/gauge.h>
#include <wx/jsonval.h>
#include <wx/jsonwriter.h>
#include <wx/panel.h>
#include <wx/regex.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>  // wxLaunchDefaultApplication, wxBusyCursor

#include <fstream>
#include <map>

#include "ocpn_plugin.h"  // SendPluginMessage
#include "wf_api.h"

enum {
  ID_WF_DLCANCEL = wxID_HIGHEST + 30,
  ID_WF_DLTIMER,
  ID_WF_DLOPEN_FOLDER,
  ID_WF_DLSET_INPUT,
  ID_WF_DLSET_SAVE,
  ID_WF_DLLOAD,
  ID_WF_DLLOAD_TOGGLE_BASE,    // checkbox/radio ids derive from this + index
};

static wxString FmtKb(long bytes) {
  // kB with thousand-separator commas, locale-independent so we don't risk
  // MSVC C4819 issues with locale-specific number formatting (e.g. NBSP as
  // separator in some European locales). Hand-insert ',' every 3 digits.
  long kb = bytes / 1024;
  wxString num = wxString::Format("%ld", kb);
  const int len = static_cast<int>(num.length());
  for (int i = len - 3; i > 0; i -= 3) num.insert(i, ",");
  return num + " kB";
}

static const wxColour COL_OK_GREEN(0x16, 0x88, 0x40);
static const wxColour COL_ERR_RED(0xc0, 0x40, 0x40);

static wxString PrettyCategory(const wxString& cat) {
  if (cat == "atmosphere") return _("Atmosphere");
  if (cat == "wave") return _("Waves");
  if (cat == "current") return _("Currents");
  return cat;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

WfDownloadProgress::WfDownloadProgress(wxWindow* parent, const wxString& token,
                                       const wxString& query,
                                       const wxString& out_path)
    : wxDialog(parent, wxID_ANY, _("Downloading GRIB"), wxDefaultPosition,
               wxDefaultSize, wxCAPTION),
      m_timer(this, ID_WF_DLTIMER),
      m_token(token) {
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
                                       std::vector<WfDownloadJob> jobs,
                                       std::vector<wxString> existing_tags,
                                       bool can_save_as_set)
    : wxDialog(parent, wxID_ANY, _("Downloading GRIBs"), wxDefaultPosition,
               wxDefaultSize, wxCAPTION),
      m_timer(this, ID_WF_DLTIMER),
      m_existingTags(std::move(existing_tags)),
      m_canSaveAsSet(can_save_as_set),
      m_token(token) {
  for (auto& t : m_existingTags) { t = t.Lower(); t.Trim().Trim(false); }

  m_jobs = std::move(jobs);
  m_results.resize(m_jobs.size());
  m_queueMode = !m_jobs.empty();
  BuildUi(m_queueMode);
  StartWorker(token);
}

void WfDownloadProgress::BuildUi(bool queue_mode) {
  m_topSizer = new wxBoxSizer(wxVERTICAL);

  if (queue_mode) {
    // Visually-bundled section, same wxStaticBox treatment as the Save and
    // Loader panels below. Title swaps from "Downloading" to "Done - N of N
    // downloaded (M kB)" on completion (SetLabel on the wxStaticBox).
    auto* sbs =
        new wxStaticBoxSizer(wxVERTICAL, this, _("Downloading"));
    m_listBox = sbs->GetStaticBox();

    m_status = new wxStaticText(this, wxID_ANY, _("Preparing slice..."),
                                wxDefaultPosition, wxSize(520, -1));
    sbs->Add(m_status, 0, wxLEFT | wxRIGHT | wxTOP, 6);

    m_gauge =
        new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(520, 16));
    m_gauge->Pulse();
    sbs->Add(m_gauge, 0, wxEXPAND | wxALL, 6);

    m_completedPane = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                           wxSize(520, 120));
    m_completedPane->SetSizer(new wxBoxSizer(wxVERTICAL));
    m_completedPane->SetScrollRate(0, 10);
    sbs->Add(m_completedPane, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // Bottom row INSIDE the box: "Files saved to: <path>" on the left,
    // [Open folder] button on the right. Both empty/hidden until completion.
    auto* bot = new wxBoxSizer(wxHORIZONTAL);
    m_savedToText = new wxStaticText(this, wxID_ANY, wxEmptyString);
    bot->Add(m_savedToText, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_openFolderBtn = new wxButton(this, ID_WF_DLOPEN_FOLDER, _("Open folder"));
    m_openFolderBtn->Hide();
    bot->Add(m_openFolderBtn, 0, wxALIGN_CENTER_VERTICAL);
    sbs->Add(bot, 0, wxEXPAND | wxALL, 6);

    m_topSizer->Add(sbs, 0, wxEXPAND | wxALL, 8);

    // Empty placeholder for the save + loader panels (filled in
    // RenderQueueComplete once the queue finishes).
    m_postCompleteHost = new wxBoxSizer(wxVERTICAL);
    m_topSizer->Add(m_postCompleteHost, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
  } else {
    // Legacy single-job path - simple status + gauge, no wxStaticBox.
    m_status = new wxStaticText(this, wxID_ANY, _("Preparing slice..."),
                                wxDefaultPosition, wxSize(440, -1));
    m_topSizer->Add(m_status, 0, wxALL, 12);
    m_gauge =
        new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(440, 16));
    m_gauge->Pulse();
    m_topSizer->Add(m_gauge, 0, wxEXPAND | wxLEFT | wxRIGHT, 12);
  }

  // Footer button row - just the Cancel/Close button. Open folder is now
  // inline with the file list (see above).
  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->AddStretchSpacer();
  m_cancelBtn = new wxButton(this, ID_WF_DLCANCEL, _("Cancel"));
  row->Add(m_cancelBtn, 0);
  m_topSizer->Add(row, 0, wxEXPAND | wxALL, 12);

  SetSizerAndFit(m_topSizer);

  Bind(wxEVT_TIMER, &WfDownloadProgress::OnTimer, this, ID_WF_DLTIMER);
  Bind(wxEVT_BUTTON, &WfDownloadProgress::OnCancel, this, ID_WF_DLCANCEL);
  Bind(wxEVT_CLOSE_WINDOW, &WfDownloadProgress::OnClose, this);
  if (queue_mode) {
    Bind(wxEVT_BUTTON, &WfDownloadProgress::OnOpenFolder, this,
         ID_WF_DLOPEN_FOLDER);
  }

  m_watch.Start();
  m_timer.Start(200);
}

void WfDownloadProgress::StartWorker(const wxString& token) {
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
      m_jobIndex.store(static_cast<int>(i + 1));
    }
    if (!m_results.empty() && !m_results[0].ok) m_error = m_results[0].error;
    m_done.store(true);
  });
}

WfDownloadProgress::~WfDownloadProgress() {
  m_cancel.store(true);
  if (m_worker.joinable()) m_worker.join();
}

// ---------------------------------------------------------------------------
// Progress tick + completed-list updates
// ---------------------------------------------------------------------------

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
    m_status->SetLabel(prefix + wxString::Format(_("%ld / %ld kB (%d%%)"),
                                                 bytes / 1024, total / 1024, pct));
    m_gauge->SetValue(pct);
  } else {
    m_status->SetLabel(prefix +
                       wxString::Format(_("%ld kB"), bytes / 1024));
    m_gauge->Pulse();
  }

  if (m_queueMode) {
    const int complete = m_jobIndex.load();
    auto* sizer = m_completedPane->GetSizer();
    for (int i = m_completedSeen; i < complete; ++i) {
      const auto& r = m_results[i];
      wxString line;
      if (r.ok) {
        line = wxString::Format("  [OK]  %s  -  %s", r.job.label,
                                FmtKb(r.bytes));
      } else {
        line = wxString::Format("  [X]   %s  -  %s", r.job.label,
                                r.error.IsEmpty() ? _("failed") : r.error);
      }
      auto* txt = new wxStaticText(m_completedPane, wxID_ANY, line);
      if (!r.ok) txt->SetForegroundColour(COL_ERR_RED);
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
      RenderQueueComplete();
    } else {
      Finish();
    }
    return;
  }
  RenderProgress();
}

// ---------------------------------------------------------------------------
// Queue completion - swap to summary + Save/Loader panels
// ---------------------------------------------------------------------------

void WfDownloadProgress::RenderQueueComplete() {
  if (m_handoffDone) return;  // run-once guard
  m_handoffDone = true;
  m_timer.Stop();
  if (m_worker.joinable()) m_worker.join();

  // Flush any final completed entries the timer didn't get to.
  RenderProgress();

  int ok_count = 0;
  long total_bytes = 0;
  for (const auto& r : m_results) {
    if (r.ok) { ++ok_count; total_bytes += r.bytes; }
  }
  const int n = static_cast<int>(m_results.size());

  wxString parent_dir;
  for (const auto& r : m_results) {
    if (r.ok) { parent_dir = wxFileName(r.job.out_path).GetPath(); break; }
  }

  // Box title swaps to "Done - N of N downloaded (M kB)". Status text + gauge
  // hide since their info now lives in the title.
  wxString title;
  if (m_cancel.load()) {
    title = wxString::Format(_("Cancelled - %d of %d downloaded (%s)"),
                             ok_count, n, FmtKb(total_bytes));
  } else if (ok_count == n) {
    title = wxString::Format(_("Done - %d of %d downloaded (%s)"), ok_count, n,
                             FmtKb(total_bytes));
  } else {
    title = wxString::Format(_("Done - %d of %d succeeded (%s)"), ok_count, n,
                             FmtKb(total_bytes));
  }
  if (m_listBox) m_listBox->SetLabel(title);
  m_status->Hide();
  m_gauge->Hide();

  if (m_savedToText) {
    m_savedToText->SetLabel(parent_dir.IsEmpty()
                                ? wxString()
                                : _("Files saved to: ") + parent_dir);
  }
  if (m_openFolderBtn) {
    m_openFolderBtn->Show(!parent_dir.IsEmpty());
    m_openFolderBtn->SetClientObject(new wxStringClientData(parent_dir));
  }

  // Save-as-set + loader panels go into the post-complete host.
  if (m_canSaveAsSet && ok_count > 0) BuildSavePanel(m_postCompleteHost);
  if (ok_count > 0) BuildLoaderPanel(m_postCompleteHost);

  m_cancelBtn->SetLabel(_("Close"));
  m_cancelBtn->Enable();
  ValidateSetName();         // sets button label if any default text
  RefreshLoadButtonEnabled();

  Layout();
  Fit();
}

// ---------------------------------------------------------------------------
// "Save as set" panel
// ---------------------------------------------------------------------------

void WfDownloadProgress::BuildSavePanel(wxSizer* host) {
  m_savePanel = new wxPanel(this, wxID_ANY);
  auto* sb = new wxStaticBoxSizer(
      wxVERTICAL, m_savePanel,
      _("Save as Set on WeatherFiles.com (optional)"));

  auto* row = new wxBoxSizer(wxHORIZONTAL);
  row->Add(new wxStaticText(m_savePanel, wxID_ANY, _("Set name:")), 0,
           wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
  m_saveInput = new wxTextCtrl(m_savePanel, ID_WF_DLSET_INPUT, wxEmptyString,
                               wxDefaultPosition, wxSize(160, -1));
  m_saveInput->SetMaxLength(20);
  row->Add(m_saveInput, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  // Hint takes the flex slot so it sits to the right of the input and
  // pushes the Save button all the way to the right edge of the row.
  m_saveHint = new wxStaticText(
      m_savePanel, wxID_ANY,
      _("(lowercase letters, digits, hyphens; max 20 chars)"));
  row->Add(m_saveHint, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
  m_saveBtn = new wxButton(m_savePanel, ID_WF_DLSET_SAVE, _("Save"));
  m_saveBtn->Disable();
  row->Add(m_saveBtn, 0, wxALIGN_CENTER_VERTICAL);
  sb->Add(row, 0, wxEXPAND | wxALL, 6);

  m_savePanel->SetSizer(sb);
  host->Add(m_savePanel, 0, wxEXPAND | wxBOTTOM, 8);

  m_saveInput->Bind(wxEVT_TEXT, &WfDownloadProgress::OnSetNameChanged, this);
  m_saveBtn->Bind(wxEVT_BUTTON, &WfDownloadProgress::OnSavePressed, this);
}

void WfDownloadProgress::OnSetNameChanged(wxCommandEvent&) { ValidateSetName(); }

void WfDownloadProgress::ValidateSetName() {
  if (!m_canSaveAsSet || !m_saveInput) {
    m_currentTag.clear();
    m_currentTagValid = false;
    return;
  }
  wxString raw = m_saveInput->GetValue();
  raw.Trim().Trim(false);
  const wxString cleaned = raw.Lower();
  m_currentTag = cleaned;
  m_currentTagValid = false;

  auto set_state = [this](const wxString& msg, const wxColour& col, bool valid) {
    m_saveHint->SetLabel(msg);
    m_saveHint->SetForegroundColour(col);
    if (m_saveBtn) m_saveBtn->Enable(valid);
    m_currentTagValid = valid;
    Layout();
  };

  if (cleaned.IsEmpty()) {
    set_state(_("(lowercase letters, digits, hyphens; max 20 chars)"),
              wxNullColour, false);
    return;
  }
  wxRegEx re("^[a-z0-9-]+$");
  if (cleaned.length() > 20 || !re.Matches(cleaned)) {
    set_state(_("Use lowercase letters, digits, hyphens (max 20)"),
              COL_ERR_RED, false);
    return;
  }
  for (const auto& t : m_existingTags) {
    if (t == cleaned) {
      set_state(wxString::Format(_("'%s' already exists"), cleaned),
                COL_ERR_RED, false);
      return;
    }
  }
  set_state(_("Name is available"), COL_OK_GREEN, true);
}

void WfDownloadProgress::OnSavePressed(wxCommandEvent&) {
  if (!m_currentTagValid) return;
  if (!PerformSaveSlices()) {
    // PerformSaveSlices wrote the error into m_saveHint; let user retry.
    return;
  }
  // Success: lock down the input + button + hint. The footer Close button
  // stays as Close throughout - the user dismisses the dialog manually.
  m_saveInput->Disable();
  m_saveBtn->Disable();
  m_saveHint->SetLabel(
      wxString::Format(_("Saved as '%s'."), m_currentTag));
  m_saveHint->SetForegroundColour(COL_OK_GREEN);
  // Add the new tag to the local existing list so a subsequent rename (if
  // we add one later) would collision-detect against it.
  m_existingTags.push_back(m_currentTag);
  Layout();
}

// ---------------------------------------------------------------------------
// "Open in GRIB viewer" panel - per-category picker
// ---------------------------------------------------------------------------

void WfDownloadProgress::BuildLoaderPanel(wxSizer* host) {
  m_loaderPanel = new wxPanel(this, wxID_ANY);
  auto* sb = new wxStaticBoxSizer(wxVERTICAL, m_loaderPanel,
                                  _("Open in GRIB viewer (optional)"));

  // Group successful results by category, preserving category order.
  std::map<wxString, std::vector<const WfDownloadResult*>> by_cat;
  for (const auto& r : m_results) {
    if (!r.ok) continue;
    by_cat[r.job.category].push_back(&r);
  }
  const wxString cat_order[] = {"atmosphere", "wave", "current"};

  for (const auto& cat : cat_order) {
    auto it = by_cat.find(cat);
    if (it == by_cat.end() || it->second.empty()) continue;
    LoaderEntry entry;
    entry.category = cat;
    entry.results = it->second;

    auto* catSizer = new wxBoxSizer(wxVERTICAL);
    if (entry.results.size() == 1) {
      auto* hdr =
          new wxStaticText(m_loaderPanel, wxID_ANY, PrettyCategory(cat));
      wxFont f = hdr->GetFont(); f.MakeBold(); hdr->SetFont(f);
      catSizer->Add(hdr, 0, wxBOTTOM, 2);
      entry.check = new wxCheckBox(
          m_loaderPanel, wxID_ANY,
          entry.results[0]->job.label);
      entry.check->SetValue(true);   // default ON for single-model category
      entry.check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        RefreshLoadButtonEnabled();
      });
      catSizer->Add(entry.check, 0, wxLEFT, 16);
    } else {
      // >1 models in category: collapse to a dropdown to keep the row
      // compact regardless of the number of choices. First item is the
      // "skip this category" sentinel.
      auto* row = new wxBoxSizer(wxHORIZONTAL);
      auto* hdr =
          new wxStaticText(m_loaderPanel, wxID_ANY, PrettyCategory(cat) + ":");
      wxFont f = hdr->GetFont(); f.MakeBold(); hdr->SetFont(f);
      row->Add(hdr, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
      entry.choice = new wxChoice(m_loaderPanel, wxID_ANY);
      entry.choice->Append(_("--- none ---"));
      for (const auto* r : entry.results) {
        entry.choice->Append(r->job.label);
      }
      entry.choice->SetSelection(0);   // default: none
      entry.choice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        RefreshLoadButtonEnabled();
      });
      row->Add(entry.choice, 1, wxALIGN_CENTER_VERTICAL);
      catSizer->Add(row, 0, wxEXPAND | wxLEFT, 16);
    }
    sb->Add(catSizer, 0, wxEXPAND | wxALL, 6);
    m_loaderEntries.push_back(entry);
  }

  auto* loadRow = new wxBoxSizer(wxHORIZONTAL);
  loadRow->AddStretchSpacer();
  m_loadBtn = new wxButton(m_loaderPanel, ID_WF_DLLOAD,
                           _("Load selected models in GRIB viewer"));
  m_loadBtn->Disable();
  loadRow->Add(m_loadBtn, 0);
  sb->Add(loadRow, 0, wxEXPAND | wxALL, 6);

  // Confirmation line shown after each Load click.
  m_loaderStatus = new wxStaticText(m_loaderPanel, wxID_ANY, wxEmptyString);
  sb->Add(m_loaderStatus, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);

  m_loaderPanel->SetSizer(sb);
  host->Add(m_loaderPanel, 0, wxEXPAND | wxBOTTOM, 8);
  m_loadBtn->Bind(wxEVT_BUTTON, &WfDownloadProgress::OnLoadSelected, this);
}

void WfDownloadProgress::RefreshLoadButtonEnabled() {
  if (!m_loadBtn) return;
  bool any = false;
  for (const auto& e : m_loaderEntries) {
    if (e.check && e.check->IsChecked()) { any = true; break; }
    if (e.choice && e.choice->GetSelection() > 0) { any = true; break; }
  }
  m_loadBtn->Enable(any);
}

void WfDownloadProgress::OnLoadSelected(wxCommandEvent&) {
  LoadSelectedInViewer();
}

void WfDownloadProgress::LoadSelectedInViewer() {
  // Collect the selected per-category files.
  std::vector<const WfDownloadResult*> picks;
  for (const auto& e : m_loaderEntries) {
    if (e.check && e.check->IsChecked()) picks.push_back(e.results[0]);
    if (e.choice) {
      const int sel = e.choice->GetSelection();
      if (sel > 0 && static_cast<size_t>(sel - 1) < e.results.size()) {
        picks.push_back(e.results[sel - 1]);
      }
    }
  }
  if (picks.empty()) return;

  // Concatenate the picked GRIB2 files into <batch_dir>/combined.grib2.
  // GRIB2 is a self-delimiting message stream, so a binary cat works - the
  // grib_pi GribReader walks each message and merges by (param, level, ...)
  // key. We rewrite combined.grib2 on every Load click so re-selection
  // works.
  const wxString dir = wxFileName(picks.front()->job.out_path).GetPath();
  const wxString combined = wxFileName(dir, "combined.grib2").GetFullPath();
  {
    std::ofstream out(combined.utf8_string(),
                      std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return;
    for (const auto* r : picks) {
      std::ifstream in(r->job.out_path.utf8_string(), std::ios::binary);
      if (!in.is_open()) continue;
      out << in.rdbuf();
    }
  }

  // Hand off the single combined file to grib_pi.
  wxJSONValue cfg;
  cfg[_T("grib_file")] = combined;
  wxJSONWriter writer(wxJSONWRITER_NONE);
  wxString body;
  writer.Write(cfg, body);
  SendPluginMessage(_T("GRIB_APPLY_JSON_CONFIG"), body);

  if (m_loaderStatus) {
    m_loaderStatus->SetLabel(wxString::Format(
        _("Loaded %zu model(s) into GRIB viewer."), picks.size()));
    m_loaderStatus->SetForegroundColour(COL_OK_GREEN);
    Layout();
  }
}

// ---------------------------------------------------------------------------
// Close vs Save-and-close
// ---------------------------------------------------------------------------

bool WfDownloadProgress::PerformSaveSlices() {
  // POST one /v1/slices per successful download with the tag attached. We
  // run this synchronously on the GUI thread (a handful of fast network
  // round-trips); the alternative thread juggling isn't worth it for what
  // is normally a sub-second batch. The dialog is small enough that a
  // wxBusyCursor + status update is sufficient feedback.
  if (m_currentTag.IsEmpty()) return false;
  wxBusyCursor busy;
  m_saveHint->SetLabel(_("Saving..."));
  m_saveHint->SetForegroundColour(wxNullColour);
  Layout();
  Update();

  WfApi api(m_token);
  std::vector<wxString> tags = {m_currentTag};
  int saved = 0;
  for (const auto& r : m_results) {
    if (!r.ok) continue;
    // Slice-replay jobs don't carry the structured fields, so they cannot be
    // re-saved; the wizard sets can_save_as_set=false for that path anyway.
    if (r.job.model_id.IsEmpty()) continue;
    bool ok = false;
    wxString err;
    api.CreateSlice(r.job.model_id, r.job.params, r.job.bbox,
                    r.job.time_window_h, r.job.time_step_h,
                    r.job.spatial_factor, tags,
                    [&ok, &err](bool o, const wxString&, const wxString& e) {
                      ok = o;
                      err = e;
                    });
    if (!ok) {
      m_saveHint->SetLabel(
          wxString::Format(_("Save failed: %s (saved %d of N)"), err, saved));
      m_saveHint->SetForegroundColour(COL_ERR_RED);
      Layout();
      return false;
    }
    ++saved;
  }
  return true;
}

void WfDownloadProgress::OnOpenFolder(wxCommandEvent&) {
  if (!m_openFolderBtn) return;
  auto* data =
      static_cast<wxStringClientData*>(m_openFolderBtn->GetClientObject());
  if (data) wxLaunchDefaultApplication(data->GetData());
}

void WfDownloadProgress::OnCancel(wxCommandEvent&) {
  if (!m_done.load()) {
    // Still downloading - this is Cancel.
    m_cancel.store(true);
    m_cancelBtn->Disable();
    m_status->SetLabel(_("Cancelling..."));
    return;
  }
  // Post-completion: Close. Save is its own explicit button now; Close never
  // saves implicitly.
  EndModal(m_ok.load() ? wxID_OK : wxID_CANCEL);
}

void WfDownloadProgress::OnClose(wxCloseEvent& evt) {
  if (!m_done.load()) {
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
