/**
 * PiShockClient.cpp
 * V1.0.0
 *
 * Adds PiShock API integration to YipOS for remote control of PiShock
 * devices.
 */

#include "PiShockClient.hpp"
#include "core/Logger.hpp"
#include <algorithm>
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

PiShockClient::PiShockClient() {
  curl_global_init(CURL_GLOBAL_ALL);
  curl_ = curl_easy_init();
}

PiShockClient::~PiShockClient() {
  if (curl_)
    curl_easy_cleanup(curl_);
  curl_global_cleanup();
}

void PiShockClient::SetCredentials(const std::string &username,
                                   const std::string &apikey) {
  if (username_ != username || apikey_ != apikey) {
    username_ = username;
    apikey_ = apikey;
    token_valid_ = false;
  }
}

std::string PiShockClient::URLEncode(const std::string &value) {
  if (!curl_)
    return value;

  char *output =
      curl_easy_escape(curl_, value.c_str(), static_cast<int>(value.length()));
  if (output) {
    std::string result(output);
    curl_free(output);
    return result;
  }
  return value;
}

bool PiShockClient::FetchShockers() {
  if (!enabled_ || !HasConfig()) {
    Logger::Debug("PiShockClient: Skipping fetch (disabled or no credentials)");
    shockers_.clear();
    return true;
  }

  Logger::Info("PiShockClient: Fetching shockers");

  shockers_.clear();
  std::string response;

  // 1. Resolve Username to Numeric UserId
  // Following doc:
  // https://auth.pishock.com/Auth/GetUserIfAPIKeyValid?apikey={apikey}&username={username}
  std::string encoded_user = URLEncode(username_);
  std::string encoded_key = URLEncode(apikey_);

  std::string auth_url =
      "https://auth.pishock.com/Auth/GetUserIfAPIKeyValid?apikey=" +
      encoded_key + "&username=" + encoded_user;

  std::string user_id = "";
  if (PerformGet(auth_url, response)) {
    Logger::Debug("PiShockClient: Raw Auth Response: " + response);

    try {
      auto j = json::parse(response);

      // Check for both "UserId" and "UserID"
      if (j.contains("UserId") && !j["UserId"].is_null()) {
        user_id = j["UserId"].is_number()
                      ? std::to_string(j["UserId"].get<int>())
                      : j["UserId"].get<std::string>();
      } else if (j.contains("UserID") && !j["UserID"].is_null()) {
        user_id = j["UserID"].is_number()
                      ? std::to_string(j["UserID"].get<int>())
                      : j["UserID"].get<std::string>();
      }
    } catch (const std::exception &e) {
      Logger::Warning("PiShockClient: JSON parse error: " +
                      std::string(e.what()));
    }
  }

  // If the ID is "0" or empty, the API Key/Username combo is likely wrong
  if (user_id.empty() || user_id == "0") {
    Logger::Error(
        "PiShockClient: Auth failed. Check logs for Raw Auth Response.");
    token_valid_ = false;
    return false;
  }

  Logger::Info("PiShockClient: Authenticating with UserID: " + user_id);
  bool any_success = false;

  // Step 2: Fetch owned shockers
  response.clear();
  Logger::Info("PiShockClient: Requesting own devices");
  std::string own_url =
      "https://ps.pishock.com/PiShock/GetUserDevices?UserId=" + user_id +
      "&Token=" + encoded_key + "&api=true";
  if (PerformGet(own_url, response)) {
    if (ParseUserDevices(response))
      any_success = true;
  }

  // Step 3: Fetch share codes
  response.clear();
  Logger::Info("PiShockClient: Requesting shared devices");
  std::string codes_url =
      "https://ps.pishock.com/PiShock/GetShareCodesByOwner?UserId=" + user_id +
      "&Token=" + encoded_key + "&api=true";

  if (PerformGet(codes_url, response)) {
    Logger::Debug("PiShockClient: Raw Share Codes Response: " + response);

    std::vector<std::string> codes;
    try {
      auto j = json::parse(response);

      // If it's an object like {"Name": [12345]}
      if (j.is_object()) {
        for (auto &element : j.items()) {
          if (element.value().is_array()) {
            for (const auto &code : element.value()) {
              if (code.is_number()) {
                codes.push_back(std::to_string(code.get<int>()));
              } else if (code.is_string()) {
                codes.push_back(code.get<std::string>());
              }
            }
          }
        }
      }
      // Fallback for standard array format
      else if (j.is_array()) {
        for (const auto &item : j) {
          if (item.is_string())
            codes.push_back(item.get<std::string>());
          else if (item.is_object() && item.contains("Code")) {
            codes.push_back(item["Code"].is_string()
                                ? item["Code"].get<std::string>()
                                : std::to_string(item["Code"].get<int>()));
          }
        }
      }
    } catch (const std::exception &e) {
      Logger::Warning("PiShockClient: Error parsing share codes: " +
                      std::string(e.what()));
    }

    if (!codes.empty()) {
      // Step 4: Resolve share codes to device info
      std::string resolve_url =
          "https://ps.pishock.com/PiShock/GetShockersByShareIds?UserId=" +
          user_id + "&Token=" + encoded_key + "&api=true";
      for (const auto &code : codes) {
        resolve_url += "&shareIds=" + URLEncode(code);
      }

      response.clear();
      if (PerformGet(resolve_url, response)) {
        Logger::Debug("PiShockClient: Shared Devices Response: " + response);
        if (ParseSharedDevices(response))
          any_success = true;
      }
    }
  }

  token_valid_ = any_success;
  Logger::Debug("PiShockClient: Fetched " + std::to_string(shockers_.size()) +
                " shockers");
  return any_success;
}

bool PiShockClient::ParseUserDevices(const std::string &json_str) {
  try {
    auto j = json::parse(json_str);
    if (!j.is_array())
      return false;

    for (const auto &hub : j) {
      std::string hub_name =
          hub.contains("Name") ? hub["Name"].get<std::string>() : "Hub";
      if (hub.contains("Shockers") && hub["Shockers"].is_array()) {
        for (const auto &item : hub["Shockers"]) {
          Shocker s;
          s.id = item.contains("Code") ? item["Code"].get<std::string>() : "";
          s.name = "[" + hub_name + "] " +
                   (item.contains("Name") ? item["Name"].get<std::string>()
                                          : "Shocker");
          s.is_owned = true;
          s.backend = "pishock";
          if (!s.id.empty())
            shockers_.push_back(s);
        }
      }
    }
    return true;
  } catch (const std::exception &e) {
    Logger::Warning("PiShockClient: JSON parse error (user devices): " +
                    std::string(e.what()));
    return false;
  }
}

bool PiShockClient::ParseSharedDevices(const std::string &json_str) {
  try {
    auto j = json::parse(json_str);

    if (!j.is_object())
      return false;

    bool found_any = false;

    // Iterate through the keys
    for (auto &user_entry : j.items()) {
      if (user_entry.value().is_array()) {
        for (const auto &item : user_entry.value()) {
          Shocker s;

          if (item.contains("shareCode")) {
            s.id = item["shareCode"].get<std::string>();
          } else if (item.contains("shareId")) {
            s.id = item["shareId"].is_number()
                       ? std::to_string(item["shareId"].get<int>())
                       : item["shareId"].get<std::string>();
          }

          s.name = item.contains("shockerName")
                       ? item["shockerName"].get<std::string>()
                       : "Shared Shocker";
          s.is_owned = false;
          s.backend = "pishock";

          if (!s.id.empty()) {
            shockers_.push_back(s);
            found_any = true;
          }
        }
      }
    }
    return found_any;
  } catch (const std::exception &e) {
    Logger::Warning("PiShockClient: JSON parse error (shared devices): " +
                    std::string(e.what()));
    return false;
  }
}

bool PiShockClient::SendControl(const std::string &shocker_id,
                                const std::string &type, float intensity,
                                int duration_ms) {
  if (!HasConfig())
    return false;

  // 0=Shock, 1=Vibrate, 2=Sound
  int type_int = 1;
  if (type.find("SHOCK") != std::string::npos)
    type_int = 0;
  else if (type.find("VIBE") != std::string::npos)
    type_int = 1;
  else if (type.find("SOUND") != std::string::npos)
    type_int = 2;

  // PiShock requires duration in seconds (1-15 range)
  int duration_s = std::clamp(static_cast<int>(duration_ms / 1000), 1, 15);
  int intensity_int = std::clamp(static_cast<int>(intensity), 1, 100);

  json payload = {{"Username", username_}, // apioperate uses Username string
                  {"Apikey", apikey_},      {"Code", shocker_id},
                  {"Name", "YipOS"},        {"Op", type_int},
                  {"Duration", duration_s}, {"Intensity", intensity_int}};

  Logger::Info("PiShock: Sending " + type + " (" +
               std::to_string(intensity_int) + "%, " +
               std::to_string(duration_s) + "s) to " + shocker_id);

  return PerformPost("https://do.pishock.com/api/apioperate/", payload.dump());
}

bool PiShockClient::PerformGet(const std::string &url, std::string &response) {
  if (!curl_)
    return false;

  curl_easy_reset(curl_);
  curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl_, CURLOPT_USERAGENT, USER_AGENT);
  curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);

  CURLcode res = curl_easy_perform(curl_);

  if (res != CURLE_OK) {
    Logger::Warning("PiShockClient: GET failed: " +
                    std::string(curl_easy_strerror(res)));
    return false;
  }

  long http_code = 0;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code < 200 || http_code >= 300) {
    Logger::Warning("PiShockClient: GET " + url + " failed (HTTP " +
                    std::to_string(http_code) + ")");
    return false;
  }

  return true;
}

bool PiShockClient::PerformPost(const std::string &url,
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
  curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl_);
  curl_slist_free_all(headers);

  if (res != CURLE_OK) {
    Logger::Warning("PiShockClient: POST failed: " +
                    std::string(curl_easy_strerror(res)));
    return false;
  }

  // Handle PiShock-specific "Not Authorized" within HTTP 200 response
  if (response.find("Not Authorized") != std::string::npos) {
    token_valid_ = false;
    Logger::Warning("PiShockClient: Auth failed (Not Authorized)");
    return false;
  }

  long http_code = 0;
  curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

  if (http_code >= 200 && http_code < 300) {
    token_valid_ = true;
    Logger::Info("PiShockClient: Command sent successfully (HTTP " +
                 std::to_string(http_code) + ")");
    return true;
  }

  Logger::Warning("PiShockClient: POST failed (HTTP " +
                  std::to_string(http_code) + ")");
  return false;
}

} // namespace YipOS