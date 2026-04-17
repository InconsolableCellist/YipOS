/**
 * ShockManager.cpp
 * V1.0.0
 *
 * Manages multiple shock device APIs (OpenShock, PiShock) for YipOS.
 *
 * By otter_oasis
 */

#include "ShockManager.hpp"
#include "OpenShockClient.hpp"
#include "PiShockClient.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"

namespace YipOS {

ShockManager::ShockManager() {
  openshock_ = std::make_unique<OpenShockClient>();
  pishock_ = std::make_unique<PiShockClient>();
}

ShockManager::~ShockManager() = default;

void ShockManager::InitFromConfig(Config &config) {
  Logger::Info("ShockManager: Initialising from config");

  // OpenShock
  std::string os_enabled = config.GetState("openshock.enabled", "0");
  openshock_->SetEnabled(os_enabled != "0");
  openshock_->SetToken(config.GetState("openshock.token", ""));
  Logger::Info(std::string("ShockManager: OpenShock ") +
               (os_enabled != "0" ? "enabled" : "disabled"));

  // PiShock
  std::string ps_enabled = config.GetState("pishock.enabled", "0");
  pishock_->SetEnabled(ps_enabled != "0");
  pishock_->SetCredentials(config.GetState("pishock.username", ""),
                           config.GetState("pishock.apikey", ""));
  Logger::Info(std::string("ShockManager: PiShock ") +
               (ps_enabled != "0" ? "enabled" : "disabled"));

  FetchShockers();
}

void ShockManager::FetchShockers() {
  shockers_.clear();
  Logger::Info("ShockManager: Fetching shockers from all enabled backends");

  if (openshock_->IsEnabled()) {
    openshock_->FetchShockers();
    const auto &os_list = openshock_->GetShockers();
    shockers_.insert(shockers_.end(), os_list.begin(), os_list.end());
    Logger::Info("ShockManager: OpenShock returned " +
                 std::to_string(os_list.size()) + " shocker(s)");
  } else {
    Logger::Debug("ShockManager: OpenShock skipped (disabled)");
  }

  if (pishock_->IsEnabled()) {
    pishock_->FetchShockers();
    const auto &ps_list = pishock_->GetShockers();
    shockers_.insert(shockers_.end(), ps_list.begin(), ps_list.end());
    Logger::Info("ShockManager: PiShock returned " +
                 std::to_string(ps_list.size()) + " shocker(s)");
  } else {
    Logger::Debug("ShockManager: PiShock skipped (disabled)");
  }

  Logger::Info("ShockManager: Total shockers available: " +
               std::to_string(shockers_.size()));
}

bool ShockManager::SendControl(const std::string &shocker_id,
                               const std::string &backend,
                               const std::string &type, float intensity,
                               int duration_ms) {
  Logger::Info("ShockManager: Routing " + type + " → backend='" + backend +
               "' id='" + shocker_id + "'");
  if (backend == "openshock" && openshock_->IsEnabled()) {
    bool ok = openshock_->SendControl(shocker_id, type, intensity, duration_ms);
    if (!ok)
      Logger::Warning("ShockManager: OpenShock command failed");
    return ok;
  } else if (backend == "pishock" && pishock_->IsEnabled()) {
    bool ok = pishock_->SendControl(shocker_id, type, intensity, duration_ms);
    if (!ok)
      Logger::Warning("ShockManager: PiShock command failed");
    return ok;
  }
  Logger::Warning("ShockManager: No enabled backend matched '" + backend +
                  "' — command dropped");
  return false;
}

int ShockManager::GetMinDurationMs(const std::string &backend) const {
  if (backend == "openshock")
    return openshock_->GetMinDurationMs();
  if (backend == "pishock")
    return pishock_->GetMinDurationMs();
  return 100; // default
}

int ShockManager::GetMaxDurationMs(const std::string &backend) const {
  if (backend == "openshock")
    return openshock_->GetMaxDurationMs();
  if (backend == "pishock")
    return pishock_->GetMaxDurationMs();
  return 15000; // default
}

bool ShockManager::HasAnyConfig() const {
  return openshock_->HasConfig() || pishock_->HasConfig();
}

bool ShockManager::IsHealthy() const {
  // If a service IS configured but its token/auth is NOT valid, manager is
  // unhealthy.
  if (openshock_->IsEnabled() && openshock_->HasConfig() &&
      !openshock_->IsTokenValid()) {
    return false;
  }
  if (pishock_->IsEnabled() && pishock_->HasConfig() &&
      !pishock_->IsTokenValid()) {
    return false;
  }
  return true;
}

} // namespace YipOS
