/**
 * PiShockClient.hpp
 * V1.0.0
 *
 * Adds PiShock API integration to YipOS for remote control of PiShock
 * devices.
 *
 * By otter_oasis
 */

#pragma once

#include "IShockClient.hpp"
#include <string>
#include <vector>

typedef void CURL;

namespace YipOS {

class PiShockClient : public IShockClient {
public:
  PiShockClient();
  ~PiShockClient() override;

  void SetCredentials(const std::string &username, const std::string &apikey);
  void SetEnabled(bool enabled) override { enabled_ = enabled; }
  bool IsEnabled() const override { return enabled_; }
  bool HasConfig() const override {
    return !username_.empty() && !apikey_.empty();
  }
  bool IsTokenValid() const override { return token_valid_; }

  bool FetchShockers() override;
  const std::vector<Shocker> &GetShockers() const override { return shockers_; }

  bool SendControl(const std::string &shocker_id, const std::string &type,
                   float intensity, int duration_ms) override;

  int GetMinDurationMs() const override { return 1000; }
  int GetMaxDurationMs() const override { return 15000; }

private:
  bool PerformPost(const std::string &url, const std::string &payload);
  bool PerformGet(const std::string &url, std::string &response);
  bool ParseUserDevices(const std::string &json_str);
  bool ParseSharedDevices(const std::string &json_str);

  std::string URLEncode(const std::string &value);

  CURL *curl_ = nullptr;
  std::string username_;
  std::string apikey_;
  bool token_valid_ = false;
  bool enabled_ = true;
  std::vector<Shocker> shockers_;

  static constexpr const char *USER_AGENT = "YipOS/1.0";
};

} // namespace YipOS
