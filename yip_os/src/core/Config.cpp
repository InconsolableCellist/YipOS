#include "Config.hpp"
#include "Logger.hpp"
#include <INIReader.h>
#include <fstream>
#include <filesystem>

namespace YipOS {

bool Config::LoadFromFile(const std::string& path) {
    if (!std::filesystem::exists(path)) {
        Logger::Info("Config file not found, creating default: " + path);
        CreateDefault(path);
    }

    INIReader reader(path);
    if (reader.ParseError() < 0) {
        Logger::Error("Failed to parse config: " + path);
        return false;
    }

    osc_ip          = reader.Get("osc", "ip", osc_ip);
    osc_send_port   = reader.GetInteger("osc", "send_port", osc_send_port);
    osc_listen_port = reader.GetInteger("osc", "listen_port", osc_listen_port);

    y_offset = static_cast<float>(reader.GetReal("display", "y_offset", y_offset));
    y_scale  = static_cast<float>(reader.GetReal("display", "y_scale", y_scale));
    y_curve  = static_cast<float>(reader.GetReal("display", "y_curve", y_curve));

    write_delay      = static_cast<float>(reader.GetReal("timing", "write_delay", write_delay));
    settle_delay     = static_cast<float>(reader.GetReal("timing", "settle_delay", settle_delay));
    refresh_interval = static_cast<float>(reader.GetReal("timing", "refresh_interval", refresh_interval));

    boot_animation = reader.GetBoolean("startup", "boot_animation", boot_animation);

    log_level = reader.Get("logging", "level", log_level);

    Logger::Info("Config loaded from " + path);
    Logger::Info("  OSC: " + osc_ip + ":" + std::to_string(osc_send_port) +
                 " (listen " + std::to_string(osc_listen_port) + ")");
    Logger::Info("  Display: y_offset=" + std::to_string(y_offset) +
                 " y_scale=" + std::to_string(y_scale) +
                 " y_curve=" + std::to_string(y_curve));
    Logger::Info("  Timing: write=" + std::to_string(write_delay) +
                 "s settle=" + std::to_string(settle_delay) + "s");
    return true;
}

bool Config::SaveToFile(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) {
        Logger::Error("Failed to save config: " + path);
        return false;
    }

    f << "[osc]\n";
    f << "ip = " << osc_ip << "\n";
    f << "send_port = " << osc_send_port << "\n";
    f << "listen_port = " << osc_listen_port << "\n";
    f << "\n";

    f << "[display]\n";
    f << "y_offset = " << y_offset << "\n";
    f << "y_scale = " << y_scale << "\n";
    f << "y_curve = " << y_curve << "\n";
    f << "\n";

    f << "[timing]\n";
    f << "write_delay = " << write_delay << "\n";
    f << "settle_delay = " << settle_delay << "\n";
    f << "refresh_interval = " << refresh_interval << "\n";
    f << "\n";

    f << "[startup]\n";
    f << "boot_animation = " << (boot_animation ? "true" : "false") << "\n";
    f << "\n";

    f << "[logging]\n";
    f << "level = " << log_level << "\n";

    Logger::Info("Config saved to " + path);
    return true;
}

void Config::CreateDefault(const std::string& path) {
    Config defaults;
    defaults.SaveToFile(path);
}

} // namespace YipOS
