/**
 * IShockClient.hpp
 * V1.0.0
 *
 * Interface for Shock API integration to YipOS for remote control of
 * OpenShock and PiShock devices.
 *
 * By otter_oasis
 */

#pragma once

#include <string>
#include <vector>

namespace YipOS {

struct Shocker {
  std::string id;
  std::string name;
  bool is_owned = false;
  std::string backend; // "openshock" or "pishock"
};

class IShockClient {
public:
  virtual ~IShockClient() = default;

  virtual void SetEnabled(bool enabled) = 0;
  virtual bool HasConfig() const = 0;
  virtual bool IsTokenValid() const = 0;
  virtual bool IsEnabled() const = 0;

  virtual bool FetchShockers() = 0;
  virtual const std::vector<Shocker> &GetShockers() const = 0;

  virtual bool SendControl(const std::string &shocker_id,
                           const std::string &type, float intensity,
                           int duration_ms) = 0;

  virtual int GetMinDurationMs() const = 0;
  virtual int GetMaxDurationMs() const = 0;
};

} // namespace YipOS
