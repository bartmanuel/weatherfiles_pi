// WeatherFiles API client implementation. See wf_api.h.
//
// HTTP uses each platform's natively-available client: libcurl on macOS/Linux
// (the CI installs libcurl-dev; macOS ships it) and WinHTTP on Windows (a
// system API). wxWebRequest is avoided because it's an optional wx component
// absent on some builds (Debian's wx3.2); libcurl dev libs aren't in the
// Windows build env either. Both paths are synchronous and share the JSON
// parsing + endpoint logic below.

#include "wf_api.h"

#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/jsonreader.h>
#include <wx/jsonval.h>

#include <cstdio>
#include <string>

#ifdef __WXMSW__
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace {

// Pull a list of strings out of a wxJSON array. Non-const ref: this wxJSON's
// operator[] has no const overload (see ocpninclude/wx/jsonval.h).
std::vector<wxString> JsonStrArray(wxJSONValue& v) {
  std::vector<wxString> out;
  if (v.IsArray())
    for (int i = 0; i < v.Size(); ++i) out.push_back(v[i].AsString());
  return out;
}

#ifndef __WXMSW__
size_t wfWriteStr(char* ptr, size_t size, size_t nmemb, void* ud) {
  static_cast<std::string*>(ud)->append(ptr, size * nmemb);
  return size * nmemb;
}
size_t wfWriteFile(char* ptr, size_t size, size_t nmemb, void* ud) {
  return fwrite(ptr, size, nmemb, static_cast<FILE*>(ud));
}
int wfXferInfo(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t,
               curl_off_t) {
  const WfProgressCb* cb = static_cast<const WfProgressCb*>(clientp);
  if (cb && *cb &&
      !(*cb)(static_cast<long>(dlnow), static_cast<long>(dltotal)))
    return 1;  // non-zero aborts the transfer
  return 0;
}
#endif

}  // namespace

WfApi::WfApi(const wxString& token, const wxString& base_url)
    : m_token(token), m_base_url(base_url) {}

#ifdef __WXMSW__

long WfApi::HttpGet(const wxString& path, wxString* out_body,
                    const wxString& out_file, wxString* err,
                    const WfProgressCb& on_progress) {
  const std::wstring url = (m_base_url + path).ToStdWstring();

  URL_COMPONENTS uc;
  ZeroMemory(&uc, sizeof(uc));
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {0};
  wchar_t urlpath[4096] = {0};
  uc.lpszHostName = host;   uc.dwHostNameLength = 255;
  uc.lpszUrlPath = urlpath; uc.dwUrlPathLength = 4095;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
    if (err) *err = "Bad URL"; return -1;
  }

  HINTERNET hSession = WinHttpOpen(L"weatherfiles_pi",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) { if (err) *err = "WinHttpOpen failed"; return -1; }
  HINTERNET hConnect = WinHttpConnect(hSession, host, uc.nPort, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); if (err) *err = "WinHttpConnect failed"; return -1; }
  const DWORD secure = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlpath, NULL,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    if (err) *err = "WinHttpOpenRequest failed"; return -1;
  }

  const std::wstring auth = L"Authorization: Bearer " + m_token.ToStdWstring();
  WinHttpAddRequestHeaders(hRequest, auth.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

  long result = -1;
  if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(hRequest, NULL)) {
    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &slen, WINHTTP_NO_HEADER_INDEX);

    long total = 0;
    {
      wchar_t cl[32] = {0};
      DWORD cllen = sizeof(cl);
      if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH,
              WINHTTP_HEADER_NAME_BY_INDEX, cl, &cllen, WINHTTP_NO_HEADER_INDEX))
        total = static_cast<long>(_wtol(cl));
    }

    FILE* fp = nullptr;
    std::string body;
    if (!out_file.IsEmpty()) fp = _wfopen(out_file.ToStdWstring().c_str(), L"wb");
    long got = 0;
    DWORD avail = 0;
    do {
      avail = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
      std::string chunk(avail, '\0');
      DWORD read = 0;
      if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0) break;
      if (fp) fwrite(chunk.data(), 1, read, fp);
      else body.append(chunk.data(), read);
      got += static_cast<long>(read);
      if (on_progress && !on_progress(got, total)) break;  // aborted
    } while (avail > 0);
    if (fp) fclose(fp);
    result = static_cast<long>(status);
    if (out_body) *out_body = wxString::FromUTF8(body.c_str());
  } else if (err) {
    *err = "Network error contacting api.weatherfiles.com";
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return result;
}

#else  // libcurl (macOS / Linux)

long WfApi::HttpGet(const wxString& path, wxString* out_body,
                    const wxString& out_file, wxString* err,
                    const WfProgressCb& on_progress) {
  static bool s_curl_inited = false;
  if (!s_curl_inited) {  // first call is on the GUI thread, so this is safe
    curl_global_init(CURL_GLOBAL_DEFAULT);
    s_curl_inited = true;
  }

  CURL* curl = curl_easy_init();
  if (!curl) { if (err) *err = "curl init failed"; return -1; }

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
  if (on_progress) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, wfXferInfo);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA,
                     const_cast<WfProgressCb*>(&on_progress));
  }

  std::string body;
  FILE* fp = nullptr;
  if (!out_file.IsEmpty()) {
    fp = fopen(out_file.utf8_string().c_str(), "wb");
    if (!fp) {
      curl_slist_free_all(hdrs); curl_easy_cleanup(curl);
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

#endif  // __WXMSW__

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
                         WfDownloadCb cb, WfProgressCb on_progress) {
  wxString err;
  const long http = HttpGet(query, nullptr, out_path, &err, on_progress);
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
