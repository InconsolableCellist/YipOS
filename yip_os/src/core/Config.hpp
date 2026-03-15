#pragma once

#include <string>

namespace YipOS {

struct Config {
    // [osc]
    std::string osc_ip = "127.0.0.1";
    int osc_send_port = 9000;
    int osc_listen_port = 9001;

    // [display]
    float y_offset = 0.0f;
    float y_scale = 1.0f;
    float y_curve = 1.0f;

    // [timing]
    float write_delay = 0.07f;
    float settle_delay = 0.04f;
    float refresh_interval = 0.0f;

    // [startup]
    bool boot_animation = true;

    // [logging]
    std::string log_level = "INFO";

    bool LoadFromFile(const std::string& path);
    bool SaveToFile(const std::string& path) const;
    static void CreateDefault(const std::string& path);
};

} // namespace YipOS
