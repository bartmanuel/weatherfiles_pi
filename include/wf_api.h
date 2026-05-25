#ifndef WF_API_H
#define WF_API_H

// WeatherFiles public-API client for the OpenCPN plugin.
//
// Talks to api.weatherfiles.com/v1. HTTP is done with wxWebRequest (wxWidgets
// 3.2): OpenCPN's OCPN_downloadFile() takes no custom-header parameter, so it
// can't send the `Authorization: Bearer wf_pat_...` token the API requires for
// /v1/auth/me and /v1/models. (Later, GRIB downloads from /v1/dl/{token} put
// the token in the URL and CAN use OCPN_downloadFile.) JSON is parsed with
// wxJSON, consistent with the rest of the plugin.
//
// All requests are asynchronous; result callbacks fire on the main (GUI) thread
// via the wxWebRequest event, so callers can update widgets directly.

#include <wx/event.h>
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

class WfApi : public wxEvtHandler {
 public:
  explicit WfApi(const wxString& token,
                 const wxString& base_url = "https://api.weatherfiles.com/v1");

  void SetToken(const wxString& token) { m_token = token; }
  const wxString& Token() const { return m_token; }

  // GET /v1/auth/me - validates the token and returns account/quota info.
  void ValidateToken(WfAccountCb on_result);

  // GET /v1/models - the model catalogue.
  void FetchModels(WfModelsCb on_result);

 private:
  // Issues an authenticated GET to base_url + path; `cb` is invoked on the GUI
  // thread with (ok, body, http_status, err).
  using RawCb = std::function<void(bool ok, const wxString& body, int status, const wxString& err)>;
  void StartGet(const wxString& path, RawCb cb);
  void OnState(class wxWebRequestEvent& evt);

  wxString m_token;
  wxString m_base_url;
  // Single request in flight at a time (the plugin calls these sequentially:
  // validate-on-save, then fetch-models-on-open). Bound once in the ctor.
  RawCb m_pending;
};

#endif  // WF_API_H
