/**
 * ShockManager.hpp
 * V1.0.0
 *
 * Manages multiple shock device APIs (OpenShock, PiShock) for YipOS.
 *
 * By otter_oasis
 */

#pragma once

#include "IShockClient.hpp"
#include <memory>
#include <string>
#include <vector>

namespace YipOS {

class Config;
class OpenShockClient;
class PiShockClient;

class ShockManager {
public:
  ShockManager();
  ~ShockManager();

  void InitFromConfig(Config &config);

  void FetchShockers();
  const std::vector<Shocker> &GetShockers() const { return shockers_; }

  bool SendControl(const std::string &shocker_id, const std::string &backend,
                   const std::string &type, float intensity, int duration_ms);

  int GetMinDurationMs(const std::string &backend) const;
  int GetMaxDurationMs(const std::string &backend) const;

  bool IsHealthy() const;
  bool HasAnyConfig() const;

  OpenShockClient *GetOpenShockClient() const { return openshock_.get(); }
  PiShockClient *GetPiShockClient() const { return pishock_.get(); }

private:
  std::unique_ptr<OpenShockClient> openshock_;
  std::unique_ptr<PiShockClient> pishock_;
  std::vector<Shocker> shockers_;
};

} // namespace YipOS
