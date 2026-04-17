/**
 * OpenShockClient.cpp
 * V1.0.0
 *
 * Adds OpenShock API integration to YipOS for remote control of OpenShock
 * devices.
 *
 * By otter_oasis
 */

#include "OpenShockClient.hpp"
#include "core/Logger.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace YipOS {

using json = nlohmann::json;

static size_t WriteCallback(void *contents, size_t size, size_t nmemb,
                            void *userp) {
  auto *str = static_cast<std::string *>(userp);
  str->append(static_cast<char *>(contents), size * nmemb);
  return size * nmemb;
}

OpenShockClient::OpenShockClient() {
  curl_global_init(CURL_GLOBAL_ALL);
  curl_ = curl_easy_init();
}

OpenShockClient::~OpenShockClient() {
  if (curl_)
    curl_easy_cleanup(curl_);
  curl_global_cleanup();
}

bool OpenShockClient::FetchShockers() {
  if (!enabled_ || !HasConfig()) {
    Logger::Debug("OpenShockClient: Skipping fetch (disabled or no token)");
    shockers_.clear();
    return true;
  }

  Logger::Info("OpenShockClient: Fetching shockers");
  Logger::Info("OpenShockClient: Authenticating with Token");
  shockers_.clear();
  std::string response;

  // Fetch owned shockers
  Logger::Info("OpenShockClient: Requesting own devices");
  if (PerformGet(std::string(API_BASE) + "/1/shockers/own", response)) {
    ParseShockers(response, true);
  }

  // Fetch shared shockers
  response.clear();
  Logger::Info("OpenShockClient: Requesting shared devices");
  if (PerformGet(std::string(API_BASE) + "/1/shockers/shared", response)) {
    ParseShockers(response, false);
  }

  Logger::Debug("OpenShockClient: Fetched " + std::to_string(shockers_.size()) +
                " shockers");
  return true;
}

bool OpenShockClient::ParseShockers(const std::string &json_str,
                                    bool is_owned) {
  try {
    auto j = json::parse(json_str);
    if (!j.contains("data") || !j["data"].is_array())
      return false;

    for (auto &hub : j["data"]) {
      std::string hub_name =
          hub.contains("name") ? hub["name"].get<std::string>() : "Hub";
      if (hub.contains("shockers") && hub["shockers"].is_array()) {
        for (auto &item : hub["shockers"]) {
          Shocker s;
          s.id = item["id"].get<std::string>();
          std::string shocker_name = item.contains("name")
                                         ? item["name"].get<std::string>()
                                         : "Shocker";
          s.name = "[" + hub_name + "] " + shocker_name;
          s.is_owned = is_owned;
          s.backend = "openshock";
          shockers_.push_back(s);
        }
      }
    }
    return true;
  } catch (const std::exception &e) {
    Logger::Warning("OpenShockClient: JSON parse error: " +
                    std::string(e.what()));
    return false;
  }
}

bool OpenShockClient::SendControl(const std::string &shocker_id,
                                  const std::string &type, float intensity,
                                  int duration_ms) {
  if (!HasConfig()) {
    return false;
  }

  // Map string type to OpenShock.Common.Models.ControlType for API
  // 0=Stop, 1=Shock, 2=Vibrate, 3=Sound
  int type_int = 2; // Default to Vibe for safety
  if (type.find("SHOCK") != std::string::npos)
    type_int = 1;
  else if (type.find("VIBE") != std::string::npos)
    type_int = 2;
  else if (type.find("SOUND") != std::string::npos)
    type_int = 3;

  json payload = {
      {"shocks", json::array({{{"id", shocker_id},
                               {"type", type_int},
                               {"intensity", static_cast<int>(intensity)},
                               {"duration", duration_ms},
                               {"exclusive", true}}})}};

  Logger::Info("OpenShock: Sending " + type + " (" +
               std::to_string(static_cast<int>(intensity)) + "%, " +
               std::to_string(duration_ms) + "ms) to " + shocker_id);

  return PerformPost(std::string(API_BASE) + "/2/shockers/control",
                     payload.dump());
}

void OpenShockClient::SetToken(const std::string &token) {
  if (token_ != token) {
    token_ = token;
    token_valid_ = false;
  }
}

bool OpenShockClient::PerformGet(const std::string &url,
                                 std::string &response) {
  if (!curl_)
    return false;

  curl_easy_reset(curl_);
  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl_, CURLOPT_USERAGENT, USER_AGENT);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, ("OpenShockToken: " + token_).c_str());
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl_);
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    Logger::Warning("OpenShockClient: GET failed: " +
                    std::string(curl_easy_strerror(res)));
    return false;
  }

  long http_code = 0;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 200 && http_code < 300) {
    token_valid_ = true;
  } else {
    if (http_code == 401)
      token_valid_ = false;
    Logger::Warning("OpenShockClient: GET " + url + " failed (HTTP " +
                    std::to_string(http_code) + ")");
    if (!response.empty()) {
      Logger::Warning("OpenShockClient: Response: " + response);
    }
  }

  return http_code == 200;
}

bool OpenShockClient::PerformPost(const std::string &url,
                                  const std::string &payload) {
  if (!curl_)
    return false;

  std::string response;
  curl_easy_reset(curl_);
  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl_, CURLOPT_USERAGENT, USER_AGENT);

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, ("OpenShockToken: " + token_).c_str());
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl_);
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    Logger::Warning("OpenShockClient: POST failed: " +
                    std::string(curl_easy_strerror(res)));
    return false;
  }

  long http_code = 0;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 200 && http_code < 300) {
    token_valid_ = true;
    Logger::Info("OpenShockClient: Command sent successfully (HTTP " +
                 std::to_string(http_code) + ")");
  } else {
    if (http_code == 401)
      token_valid_ = false;
    Logger::Warning("OpenShockClient: POST " + url + " failed (HTTP " +
                    std::to_string(http_code) + ")");
    if (!response.empty()) {
      Logger::Warning("OpenShockClient: Response: " + response);
    }
    return false;
  }

  return true;
}

} // namespace YipOS
