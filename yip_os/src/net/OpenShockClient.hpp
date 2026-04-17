/**
 * OpenShockClient.hpp
 * V1.0.0
 *
 * Adds OpenShock API integration to YipOS for remote control of OpenShock
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

class OpenShockClient : public IShockClient {
public:
  OpenShockClient();
  ~OpenShockClient() override;

  void SetToken(const std::string &token);
  void SetEnabled(bool enabled) override { enabled_ = enabled; }
  bool IsEnabled() const override { return enabled_; }
  bool HasConfig() const override { return !token_.empty(); }
  bool IsTokenValid() const override { return token_valid_; }

  // Fetch list of shockers (owned and shared)
  bool FetchShockers() override;
  const std::vector<Shocker> &GetShockers() const override { return shockers_; }

  // Control shockers
  // type: "Shock", "Vibrate", "Sound", "Stop"
  bool SendControl(const std::string &shocker_id, const std::string &type,
                   float intensity, int duration_ms) override;

  int GetMinDurationMs() const override { return 100; }
  int GetMaxDurationMs() const override { return 30000; }

private:
  bool ParseShockers(const std::string &json, bool is_owned);
  bool PerformPost(const std::string &url, const std::string &payload);
  bool PerformGet(const std::string &url, std::string &response);

  CURL *curl_ = nullptr;
  std::string token_;
  bool token_valid_ = false;
  bool enabled_ = true;
  std::vector<Shocker> shockers_;

  static constexpr const char *API_BASE = "https://api.openshock.app";
  static constexpr const char *USER_AGENT = "YipOS/1.0";
};

} // namespace YipOS
