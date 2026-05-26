// WeatherFiles API client implementation (libcurl). See wf_api.h.

#include "wf_api.h"

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/jsonreader.h>
#include <wx/jsonval.h>

#include <curl/curl.h>

#include <cstdio>
#include <string>

namespace {

size_t wfWriteStr(char* ptr, size_t size, size_t nmemb, void* ud) {
  static_cast<std::string*>(ud)->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t wfWriteFile(char* ptr, size_t size, size_t nmemb, void* ud) {
  return fwrite(ptr, size, nmemb, static_cast<FILE*>(ud));
}

// Pull a list of strings out of a wxJSON array. Non-const ref: this wxJSON's
// operator[] has no const overload (see ocpninclude/wx/jsonval.h).
std::vector<wxString> JsonStrArray(wxJSONValue& v) {
  std::vector<wxString> out;
  if (v.IsArray())
    for (int i = 0; i < v.Size(); ++i) out.push_back(v[i].AsString());
  return out;
}

}  // namespace

WfApi::WfApi(const wxString& token, const wxString& base_url)
    : m_token(token), m_base_url(base_url) {}

long WfApi::HttpGet(const wxString& path, wxString* out_body,
                    const wxString& out_file, wxString* err) {
  static bool s_curl_inited = false;
  if (!s_curl_inited) {  // single-threaded (GUI), so this guard is safe
    curl_global_init(CURL_GLOBAL_DEFAULT);
    s_curl_inited = true;
  }

  CURL* curl = curl_easy_init();
  if (!curl) {
    if (err) *err = "curl init failed";
    return -1;
  }

  const std::string url = (m_base_url + path).utf8_string();
  const std::string auth = "Authorization: Bearer " + m_token.utf8_string();
  struct curl_slist* hdrs = nullptr;
  hdrs = curl_slist_append(hdrs, auth.c_str());
  hdrs = curl_slist_append(hdrs, "Accept: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "weatherfiles_pi");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 180L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  std::string body;
  FILE* fp = nullptr;
  if (!out_file.IsEmpty()) {
    fp = fopen(out_file.utf8_string().c_str(), "wb");
    if (!fp) {
      curl_slist_free_all(hdrs);
      curl_easy_cleanup(curl);
      if (err) *err = "Cannot open output file: " + out_file;
      return -1;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wfWriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  } else {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wfWriteStr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  }

  const CURLcode rc = curl_easy_perform(curl);
  long http = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
  if (fp) fclose(fp);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) {
    if (err) *err = wxString::FromUTF8(curl_easy_strerror(rc));
    return -1;
  }
  if (out_body) *out_body = wxString::FromUTF8(body.c_str());
  return http;
}

void WfApi::ValidateToken(WfAccountCb cb) {
  wxString body, err;
  const long http = HttpGet("/auth/me", &body, wxEmptyString, &err);
  if (http < 0) { cb(false, WfAccount{}, err); return; }
  if (http == 401) { cb(false, WfAccount{}, "Unauthorized - check your API token"); return; }
  if (http != 200) { cb(false, WfAccount{}, wxString::Format("Server error (HTTP %ld)", http)); return; }

  wxJSONValue root;
  wxJSONReader reader;
  if (reader.Parse(body, &root) > 0) {
    cb(false, WfAccount{}, "Could not parse /auth/me response");
    return;
  }
  WfAccount acct;
  acct.email = root["email"].AsString();
  acct.tier = root["tier"].AsString();
  const wxJSONValue& lim = root["daily_download_limit"];
  acct.daily_download_limit = lim.IsNull() ? -1 : lim.AsInt();
  acct.daily_downloads_used_today = root["daily_downloads_used_today"].AsInt();
  acct.allowed_models = JsonStrArray(root["allowed_models"]);
  cb(true, acct, "");
}

void WfApi::FetchModels(WfModelsCb cb) {
  wxString body, err;
  const long http = HttpGet("/models", &body, wxEmptyString, &err);
  if (http < 0) { cb(false, {}, err); return; }
  if (http != 200) { cb(false, {}, wxString::Format("Server error (HTTP %ld)", http)); return; }

  wxJSONValue root;
  wxJSONReader reader;
  if (reader.Parse(body, &root) > 0 || !root.IsArray()) {
    cb(false, {}, "Could not parse /models response");
    return;
  }
  std::vector<WfModel> models;
  for (int i = 0; i < root.Size(); ++i) {
    wxJSONValue& m = root[i];
    WfModel wm;
    wm.id = m["id"].AsString();
    wm.name = m["name"].AsString();
    wm.source = m["source"].AsString();
    wxJSONValue& b = m["bounds"];
    wm.south = b["south"].AsDouble();
    wm.west = b["west"].AsDouble();
    wm.north = b["north"].AsDouble();
    wm.east = b["east"].AsDouble();
    wm.params = JsonStrArray(m["params"]);
    wm.format = m["format"].AsString();
    wm.forecast_horizon_h = m["forecast_horizon_h"].AsInt();
    wm.step_h = m["step_h"].AsDouble();
    wm.run_interval_h = m["run_interval_h"].AsInt();
    models.push_back(wm);
  }
  cb(true, models, "");
}

void WfApi::DownloadGrib(const wxString& query, const wxString& out_path,
                         WfDownloadCb cb) {
  wxString err;
  const long http = HttpGet(query, nullptr, out_path, &err);
  if (http < 0) { cb(false, err); return; }
  if (http != 200) {
    // The error body was streamed to the file; read it back for the message.
    wxString detail;
    {
      wxFFile f(out_path, "r");
      if (f.IsOpened()) f.ReadAll(&detail);
    }
    wxRemoveFile(out_path);
    cb(false, wxString::Format("HTTP %ld: %s", http, detail));
    return;
  }
  cb(true, "");
}
