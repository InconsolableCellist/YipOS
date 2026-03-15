#include "Config.hpp"
#include "Logger.hpp"
#include <INIReader.h>
#include <fstream>
#include <filesystem>

namespace YipOS {

std::string Config::GetState(const std::string& key, const std::string& fallback) const {
    auto it = state.find(key);
    return (it != state.end()) ? it->second : fallback;
}

void Config::SetState(const std::string& key, const std::string& value) {
    state[key] = value;
    if (!config_path.empty()) {
        SaveToFile(config_path);
    }
}

void Config::ClearState() {
    state.clear();
    Logger::Info("NVRAM cleared");
    if (!config_path.empty()) {
        SaveToFile(config_path);
    }
}

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
    boot_speed     = static_cast<float>(reader.GetReal("startup", "boot_speed", boot_speed));

    log_level = reader.Get("logging", "level", log_level);

    // Load [state] section — inih exposes all keys in a section via GetFields
    // Since INIReader doesn't expose section iteration, re-parse manually
    state.clear();
    std::ifstream f(path);
    if (f.is_open()) {
        std::string line;
        bool in_state = false;
        while (std::getline(f, line)) {
            // Trim leading whitespace
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            if (line[0] == '[') {
                in_state = (line.find("[state]") != std::string::npos);
                continue;
            }
            if (in_state && line[0] != '#' && line[0] != ';') {
                auto eq = line.find('=');
                if (eq != std::string::npos) {
                    std::string key = line.substr(0, eq);
                    std::string val = line.substr(eq + 1);
                    // Trim
                    key.erase(key.find_last_not_of(" \t") + 1);
                    val.erase(0, val.find_first_not_of(" \t"));
                    if (!key.empty()) {
                        state[key] = val;
                    }
                }
            }
        }
    }

    config_path = path;

    Logger::Info("Config loaded from " + path);
    Logger::Info("  OSC: " + osc_ip + ":" + std::to_string(osc_send_port) +
                 " (listen " + std::to_string(osc_listen_port) + ")");
    Logger::Info("  Display: y_offset=" + std::to_string(y_offset) +
                 " y_scale=" + std::to_string(y_scale) +
                 " y_curve=" + std::to_string(y_curve));
    Logger::Info("  Timing: write=" + std::to_string(write_delay) +
                 "s settle=" + std::to_string(settle_delay) + "s");
    if (!state.empty()) {
        Logger::Info("  NVRAM: " + std::to_string(state.size()) + " key(s)");
    }
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
    f << "boot_speed = " << boot_speed << "\n";
    f << "\n";

    f << "[logging]\n";
    f << "level = " << log_level << "\n";

    if (!state.empty()) {
        f << "\n[state]\n";
        for (auto& [key, val] : state) {
            f << key << " = " << val << "\n";
        }
    }

    Logger::Debug("Config saved to " + path);
    return true;
}

void Config::CreateDefault(const std::string& path) {
    Config defaults;
    defaults.SaveToFile(path);
}

} // namespace YipOS
