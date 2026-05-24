// WeatherFiles API client implementation. See wf_api.h.
//
// NOTE (build): this is written against wxWidgets 3.2's wxWebRequest + wxJSON
// and has NOT been compiled in this environment (no OpenCPN SDK here). Expect to
// verify/adjust during the first local build. CMake must compile src/wf_api.cpp
// and link the wxWidgets webrequest component (see docs/PLUGIN_PLAN.md).

#include "wf_api.h"

#include <wx/jsonreader.h>
#include <wx/jsonval.h>
#include <wx/webrequest.h>

WfApi::WfApi(const wxString& token, const wxString& base_url)
    : m_token(token), m_base_url(base_url) {
  Bind(wxEVT_WEBREQUEST_STATE, &WfApi::OnState, this);
}

void WfApi::StartGet(const wxString& path, RawCb cb) {
  if (m_pending) {
    cb(false, "", 0, "A request is already in progress");
    return;
  }
  wxWebSession& session = wxWebSession::GetDefault();
  if (!session.IsOpened()) {
    cb(false, "", 0, "HTTP backend unavailable");
    return;
  }
  wxWebRequest request = session.CreateRequest(this, m_base_url + path);
  if (!request.IsOk()) {
    cb(false, "", 0, "Could not create request");
    return;
  }
  request.SetHeader("Authorization", "Bearer " + m_token);
  request.SetHeader("Accept", "application/json");
  m_pending = std::move(cb);
  request.Start();
}

void WfApi::OnState(wxWebRequestEvent& evt) {
  if (!m_pending) return;
  switch (evt.GetState()) {
    case wxWebRequest::State_Completed: {
      RawCb cb = std::move(m_pending);
      m_pending = nullptr;
      const wxWebResponse& resp = evt.GetResponse();
      cb(true, resp.AsString(), static_cast<int>(resp.GetStatus()), "");
      break;
    }
    case wxWebRequest::State_Unauthorized: {
      RawCb cb = std::move(m_pending);
      m_pending = nullptr;
      cb(false, "", 401, "Unauthorized — check your API token");
      break;
    }
    case wxWebRequest::State_Failed: {
      RawCb cb = std::move(m_pending);
      m_pending = nullptr;
      cb(false, "", 0, evt.GetErrorDescription());
      break;
    }
    default:
      break;  // State_Active / State_Idle — keep waiting
  }
}

namespace {
// Pull a list of strings out of a wxJSON array value.
std::vector<wxString> JsonStrArray(const wxJSONValue& v) {
  std::vector<wxString> out;
  if (v.IsArray()) {
    for (int i = 0; i < v.Size(); ++i) out.push_back(v[i].AsString());
  }
  return out;
}
}  // namespace

void WfApi::ValidateToken(WfAccountCb on_result) {
  StartGet("/auth/me", [on_result](bool ok, const wxString& body, int status, const wxString& err) {
    if (!ok) {
      on_result(false, WfAccount{}, err);
      return;
    }
    wxJSONValue root;
    wxJSONReader reader;
    if (reader.Parse(body, &root) > 0) {
      on_result(false, WfAccount{}, "Could not parse /auth/me response");
      return;
    }
    WfAccount acct;
    acct.email = root["email"].AsString();
    acct.tier = root["tier"].AsString();
    const wxJSONValue& lim = root["daily_download_limit"];
    acct.daily_download_limit = lim.IsNull() ? -1 : lim.AsInt();  // null => unlimited
    acct.daily_downloads_used_today = root["daily_downloads_used_today"].AsInt();
    acct.allowed_models = JsonStrArray(root["allowed_models"]);
    on_result(true, acct, "");
  });
}

void WfApi::FetchModels(WfModelsCb on_result) {
  StartGet("/models", [on_result](bool ok, const wxString& body, int status, const wxString& err) {
    if (!ok) {
      on_result(false, {}, err);
      return;
    }
    wxJSONValue root;
    wxJSONReader reader;
    if (reader.Parse(body, &root) > 0 || !root.IsArray()) {
      on_result(false, {}, "Could not parse /models response");
      return;
    }
    std::vector<WfModel> models;
    for (int i = 0; i < root.Size(); ++i) {
      const wxJSONValue& m = root[i];
      WfModel wm;
      wm.id = m["id"].AsString();
      wm.name = m["name"].AsString();
      wm.source = m["source"].AsString();
      const wxJSONValue& b = m["bounds"];  // {south, west, north, east}
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
    on_result(true, models, "");
  });
}
