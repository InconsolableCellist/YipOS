#pragma once

#include <string>
#include <unordered_map>

namespace YipOS {

struct Config {
    // [osc]
    std::string osc_ip = "127.0.0.1";
    int osc_send_port = 9000;
    int osc_listen_port = 9001;
    bool osc_query_enabled = true;

    // [display]
    float y_offset = 0.0f;
    float y_scale = 1.0f;
    float y_curve = 1.0f;

    // [timing]
    float write_delay = 0.07f;
    float settle_delay = 0.04f;
    float refresh_interval = 30.0f;

    // [startup]
    bool boot_animation = true;
    float boot_speed = 1.0f;  // multiplier for boot pause durations (0.5=fast, 1.0=normal, 2.0=slow)

    // [logging]
    std::string log_level = "INFO";

    // [vrcx]
    bool vrcx_enabled = false;
    std::string vrcx_db_path; // empty = auto-detect

    // [avatar]
    std::string vrc_osc_path; // empty = auto-detect (Windows) or must configure (Linux)

    // [state] — persistent key/value store ("NVRAM")
    // Modules use dotted keys: "net.interface", "stats.disk", etc.
    std::unordered_map<std::string, std::string> state;

    std::string GetState(const std::string& key, const std::string& fallback = "") const;
    void SetState(const std::string& key, const std::string& value);
    void ClearState();

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;
    static void CreateDefault(const std::string& path);

    // Set by main after load so SetState can auto-save
    std::string config_path;
};

} // namespace YipOS
