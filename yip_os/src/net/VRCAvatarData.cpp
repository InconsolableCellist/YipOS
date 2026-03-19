#include "VRCAvatarData.hpp"
#include "core/Logger.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace YipOS {

using json = nlohmann::json;

std::string VRCAvatarData::DefaultOSCPath() {
#ifdef _WIN32
    // %LOCALAPPDATA%Low\VRChat\VRChat\OSC
    const char* local = std::getenv("LOCALAPPDATA");
    if (local) {
        return std::string(local) + "Low\\VRChat\\VRChat\\OSC";
    }
    return "";
#else
    // Linux/Proton — no reliable default, user must configure
    return "";
#endif
}

bool VRCAvatarData::Scan(const std::string& osc_path) {
    avatars_.clear();

    if (osc_path.empty() || !std::filesystem::exists(osc_path)) {
        Logger::Warning("Avatar scan: path not found: " + osc_path);
        return false;
    }

    namespace fs = std::filesystem;

    // Iterate usr_*/Avatars/*.json
    int count = 0;
    for (auto& user_dir : fs::directory_iterator(osc_path)) {
        if (!user_dir.is_directory()) continue;
        std::string dirname = user_dir.path().filename().string();
        if (dirname.substr(0, 4) != "usr_") continue;

        fs::path avatars_dir = user_dir.path() / "Avatars";
        if (!fs::exists(avatars_dir)) continue;

        for (auto& entry : fs::directory_iterator(avatars_dir)) {
            if (entry.path().extension() != ".json") continue;

            try {
                std::ifstream f(entry.path());
                if (!f.is_open()) continue;

                // Skip BOM if present
                char bom[3];
                f.read(bom, 3);
                if (!(bom[0] == '\xEF' && bom[1] == '\xBB' && bom[2] == '\xBF')) {
                    f.seekg(0);
                }

                json j = json::parse(f);

                VRCAvatarEntry avatar;
                avatar.id = j.value("id", "");
                avatar.name = j.value("name", "");

                if (j.contains("parameters") && j["parameters"].is_array()) {
                    for (auto& p : j["parameters"]) {
                        VRCAvatarParam param;
                        param.name = p.value("name", "");

                        if (p.contains("input") && p["input"].is_object()) {
                            param.input_address = p["input"].value("address", "");
                            param.input_type = p["input"].value("type", "");
                        }
                        if (p.contains("output") && p["output"].is_object()) {
                            param.output_address = p["output"].value("address", "");
                            param.output_type = p["output"].value("type", "");
                        }

                        if (!param.name.empty()) {
                            avatar.parameters.push_back(std::move(param));
                        }
                    }
                }

                if (!avatar.id.empty()) {
                    Logger::Info("Avatar found: " + avatar.name + " (" +
                                 std::to_string(avatar.parameters.size()) + " params)");
                    avatars_.push_back(std::move(avatar));
                    count++;
                }
            } catch (const std::exception& e) {
                Logger::Warning("Failed to parse avatar JSON: " +
                                entry.path().string() + " — " + e.what());
            }
        }
    }

    // Sort by name
    std::sort(avatars_.begin(), avatars_.end(),
              [](const VRCAvatarEntry& a, const VRCAvatarEntry& b) {
                  return a.name < b.name;
              });

    Logger::Info("Avatar scan complete: " + std::to_string(count) + " avatar(s)");
    return count > 0;
}

bool VRCAvatarData::DetectCurrentAvatar(const std::string& vrc_root) {
    namespace fs = std::filesystem;

    // Look in LocalAvatarData/usr_*/  for the most recently modified file
    // The filename is the avatar ID (e.g. avtr_xxxx-xxxx-...)
    fs::path local_data = fs::path(vrc_root) / "LocalAvatarData";
    if (!fs::exists(local_data)) return false;

    std::string best_id;
    fs::file_time_type best_time{};

    for (auto& user_dir : fs::directory_iterator(local_data)) {
        if (!user_dir.is_directory()) continue;
        for (auto& entry : fs::directory_iterator(user_dir.path())) {
            if (!entry.is_regular_file()) continue;
            std::string fname = entry.path().filename().string();
            if (fname.substr(0, 5) != "avtr_") continue;

            auto mtime = entry.last_write_time();
            if (best_id.empty() || mtime > best_time) {
                best_time = mtime;
                best_id = fname;
            }
        }
    }

    if (!best_id.empty() && FindById(best_id)) {
        current_avatar_id_ = best_id;
        auto* av = FindById(best_id);
        Logger::Info("Detected active avatar: " + av->name + " (" + best_id + ")");

        // Load expression params from LocalAvatarData for this avatar
        LoadExpressionParams(vrc_root, best_id);
        return true;
    }
    return false;
}

bool VRCAvatarData::LoadExpressionParams(const std::string& vrc_root, const std::string& avatar_id) {
    namespace fs = std::filesystem;
    expression_params_.clear();

    // Search LocalAvatarData/usr_*/<avatar_id>
    fs::path local_data = fs::path(vrc_root) / "LocalAvatarData";
    if (!fs::exists(local_data)) return false;

    for (auto& user_dir : fs::directory_iterator(local_data)) {
        if (!user_dir.is_directory()) continue;
        fs::path avtr_file = user_dir.path() / avatar_id;
        if (!fs::exists(avtr_file)) continue;

        try {
            std::ifstream f(avtr_file);
            if (!f.is_open()) continue;

            json j = json::parse(f);
            if (j.contains("animationParameters") && j["animationParameters"].is_array()) {
                for (auto& p : j["animationParameters"]) {
                    std::string name = p.value("name", "");
                    if (!name.empty()) {
                        expression_params_.insert(name);
                    }
                }
            }

            Logger::Info("Loaded " + std::to_string(expression_params_.size()) +
                         " expression params for " + avatar_id);
            return true;
        } catch (const std::exception& e) {
            Logger::Warning("Failed to parse LocalAvatarData: " + std::string(e.what()));
        }
    }
    return false;
}

const VRCAvatarEntry* VRCAvatarData::FindById(const std::string& id) const {
    for (auto& a : avatars_) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

bool VRCAvatarData::IsExpressionExcluded(const std::string& name) {
    // System params that appear in LocalAvatarData but aren't user toggles
    if (name == "VRCFaceBlendV" || name == "VRCFaceBlendH") return true;
    if (name == "VRCEmote") return true;
    if (name == "FT/Gestures" || name == "FT/Debug") return true;
    if (name == "EyeTrackingActive" || name == "LipTrackingActive") return true;
    if (name == "EyeDilationEnable" || name == "FacialExpressionsDisabled") return true;
    if (name == "VisemesEnable") return true;
    // GogoLoco internal state
    if (name.size() > 3 && name.substr(0, 3) == "Go/") return true;
    // State params
    if (name.size() > 6 && name.substr(0, 6) == "State/") return true;
    // CRT wrist touchpoints (internal)
    if (name.size() > 10 && name.substr(0, 10) == "CRT_Wrist_") return true;
    return false;
}

bool VRCAvatarData::IsExcludedParam(const std::string& name) {
    // Face tracking v2 (150+ params)
    if (name.size() > 5 && name.substr(0, 5) == "FT/v2") return true;
    // VRCFury tracking calibration
    if (name.size() > 2 && name.substr(0, 2) == "VF") return true;
    // GogoLoco
    if (name.size() > 3 && name.substr(0, 3) == "Go/") return true;
    // State params
    if (name.size() > 6 && name.substr(0, 6) == "State/") return true;
    // VRC system params
    if (name == "VRCFaceBlendV" || name == "VRCFaceBlendH") return true;
    if (name == "VRCEmote") return true;
    if (name == "FT/Gestures" || name == "FT/Debug") return true;
    // Smoothing
    if (name.size() > 10 && name.substr(0, 10) == "Smoothing/") return true;

    return false;
}

// Detect nested VRCFury prefix: "VF<digits>_VF<digits>_..."
// These are internal wrappers whose OSC addresses VRChat ignores;
// the single-prefix version (VF<digits>_...) is the functional one.
static bool IsNestedVFPrefix(const std::string& name) {
    if (name.size() < 6 || name[0] != 'V' || name[1] != 'F') return false;
    size_t i = 2;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') i++;
    if (i == 2 || i >= name.size() || name[i] != '_') return false;
    i++; // skip '_'
    if (i + 2 >= name.size() || name[i] != 'V' || name[i + 1] != 'F') return false;
    return true;
}

std::vector<const VRCAvatarParam*> VRCAvatarData::GetToggleParams(const std::string& avatar_id) const {
    std::vector<const VRCAvatarParam*> result;
    const VRCAvatarEntry* avatar = FindById(avatar_id);
    if (!avatar) return result;

    bool have_expression_params = !expression_params_.empty();

    for (auto& p : avatar->parameters) {
        // Must have input address
        if (p.input_address.empty()) continue;

        // Skip nested VRCFury wrappers (VF##_VF##_...) — VRChat only
        // responds to the single-prefix version (VF##_...).
        if (IsNestedVFPrefix(p.name)) continue;

        if (have_expression_params) {
            // Whitelist: only show params from LocalAvatarData (radial menu)
            if (!expression_params_.count(p.name)) continue;
            // Within the whitelist, still skip system/internal params
            if (IsExpressionExcluded(p.name)) continue;
        } else {
            // Fallback: old heuristic filter (Bool only, exclude known prefixes)
            if (p.input_type != "Bool" || p.output_type != "Bool") continue;
            if (IsExcludedParam(p.name)) continue;
        }

        result.push_back(&p);
    }

    return result;
}

} // namespace YipOS
