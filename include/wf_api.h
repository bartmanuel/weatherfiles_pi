#ifndef WF_API_H
#define WF_API_H

// WeatherFiles public-API client for the OpenCPN plugin.
//
// Talks to api.weatherfiles.com/v1 over HTTPS with libcurl. libcurl is used (not
// wxWebRequest) because wxWebRequest is an optional wx component and is absent
// on some builds (e.g. Debian's wx3.2); libcurl is available on every target
// (the CI installs libcurl-dev; macOS ships it) and supports the Authorization:
// Bearer header the API requires. JSON is parsed with wxJSON.
//
// Requests are SYNCHRONOUS: each method performs the HTTP call inline and then
// invokes its callback before returning. The dialogs call these from button
// handlers / modal loops on the GUI thread, so the callbacks update widgets
// directly. (Callers should show a wxBusyCursor around the call.)

#include <wx/string.h>

#include <functional>
#include <vector>

// A model from GET /v1/models (the subset the plugin renders).
struct WfModel {
  wxString id;
  wxString name;
  wxString source;
  // From the structured `bounds` object {south, west, north, east}.
  double west = 0, east = 0, south = 0, north = 0;
  std::vector<wxString> params;  // offered parameter keys (wind, gusts, ...)
  wxString format;               // "grib" | "image"
  int forecast_horizon_h = 0;
  double step_h = 0;
  int run_interval_h = 0;
};

// A geographic bounding box (degrees). `valid` is false until set.
struct WfBBox {
  double south = 0, west = 0, north = 0, east = 0;
  bool valid = false;
};

// Account info from GET /v1/auth/me.
struct WfAccount {
  wxString email;
  wxString tier;                          // "basic" | "pro"
  int daily_download_limit = -1;          // -1 == unlimited (JSON null)
  int daily_downloads_used_today = 0;
  std::vector<wxString> allowed_models;   // ids this tier may slice
};

using WfAccountCb = std::function<void(bool ok, const WfAccount&, const wxString& err)>;
using WfModelsCb = std::function<void(bool ok, const std::vector<WfModel>&, const wxString& err)>;
using WfDownloadCb = std::function<void(bool ok, const wxString& err)>;

class WfApi {
 public:
  explicit WfApi(const wxString& token,
                 const wxString& base_url = "https://api.weatherfiles.com/v1");

  void SetToken(const wxString& token) { m_token = token; }
  const wxString& Token() const { return m_token; }

  // GET /v1/auth/me - validates the token and returns account/quota info.
  void ValidateToken(WfAccountCb on_result);

  // GET /v1/models - the model catalogue.
  void FetchModels(WfModelsCb on_result);

  // GET /v1/grib<query> - one-shot sliceless download streamed to `out_path`.
  // `query` is the path+query starting with "/grib?..." (caller builds it).
  void DownloadGrib(const wxString& query, const wxString& out_path,
                    WfDownloadCb on_result);

 private:
  // Synchronous authenticated GET of base_url + path. If out_file is non-empty
  // the body is streamed to that file; otherwise it's returned in *out_body.
  // Returns the HTTP status, or -1 on a transport error (with err set).
  long HttpGet(const wxString& path, wxString* out_body,
               const wxString& out_file, wxString* err);

  wxString m_token;
  wxString m_base_url;
};

#endif  // WF_API_H
