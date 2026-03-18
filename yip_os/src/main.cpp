#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>

#include "core/Platform.hpp"
#include "core/PathUtils.hpp"
#include "core/Logger.hpp"
#include "core/Config.hpp"
#include "core/Glyphs.hpp"
#include "app/ScreenBuffer.hpp"
#include "app/PDADisplay.hpp"
#include "app/PDAController.hpp"
#include "net/OSCManager.hpp"
#include "net/NetTracker.hpp"
#include "net/VRCXData.hpp"
#include "net/VRCAvatarData.hpp"
#include "audio/AudioCapture.hpp"
#include "audio/WhisperWorker.hpp"
#include "platform/SystemStats.hpp"
#include "ui/UIManager.hpp"

std::atomic<bool> g_running = true;

// Run the boot sequence, rendering ImGui frames during the animation
// so the UI stays responsive.
static void RunBootWithUI(YipOS::PDAController& pda, YipOS::UIManager& ui,
                          YipOS::Config& config, YipOS::OSCManager& osc) {
    // Run boot on a background thread so UI keeps rendering
    std::atomic<bool> boot_done = false;
    std::thread boot_thread([&]() {
        pda.RunBootSequence();
        boot_done = true;
    });

    while (!boot_done && !ui.ShouldClose()) {
        ui.BeginFrame();
        ui.Render(pda, config, osc);
        ui.EndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    boot_thread.join();
}

// Initialize display and render home screen (optionally with boot animation)
static void InitDisplay(YipOS::PDADisplay& display, YipOS::PDAController& pda,
                        YipOS::UIManager& ui, YipOS::Config& config,
                        YipOS::OSCManager& osc) {
    // Cancel any active buffered session before boot/init
    display.CancelBuffered();
    display.ClearScreen();
    display.SetTextMode();

    if (config.boot_animation) {
        RunBootWithUI(pda, ui, config, osc);
        display.CancelBuffered();
        display.ClearScreen();
        display.SetTextMode();
    }

    pda.StartRender(pda.GetCurrentScreen());
}

int main(int argc, char* argv[]) {
    try {
        // Paths
        std::string configDir = YipOS::GetConfigDir();
        YipOS::EnsureDirectories(configDir);
        std::string logDir = configDir + "/logs";
        std::string configPath = configDir + "/config.ini";

        // Logger
        YipOS::Logger::Init(logDir);
        YipOS::Logger::Info("YipOS starting up");

        // Config
        YipOS::Config config;
        config.LoadFromFile(configPath);
        YipOS::Logger::SetLogLevel(YipOS::Logger::StringToLevel(config.log_level));

        // OSC
        YipOS::OSCManager osc;
        if (!osc.Initialize(config.osc_ip, config.osc_send_port, config.osc_listen_port)) {
            YipOS::Logger::Error("Failed to initialize OSC — continuing without it");
        }

        // Core PDA objects
        YipOS::ScreenBuffer screen_buffer;
        YipOS::PDADisplay display(osc, screen_buffer,
                                  config.y_offset, config.y_scale, config.y_curve,
                                  config.write_delay, config.settle_delay);
        YipOS::NetTracker net_tracker(config.GetState("net.interface"));
        YipOS::PDAController pda(display, net_tracker, config, config.refresh_interval);

        // VRCX integration
        YipOS::VRCXData vrcx_data;
        if (config.vrcx_enabled) {
            std::string db_path = config.vrcx_db_path.empty()
                ? YipOS::VRCXData::DefaultDBPath()
                : config.vrcx_db_path;
            if (vrcx_data.Open(db_path)) {
                YipOS::Logger::Info("VRCX database opened: " + db_path);
            } else {
                YipOS::Logger::Warning("VRCX database not available: " + db_path);
            }
        }
        pda.SetVRCXData(&vrcx_data);

        // Avatar management
        YipOS::VRCAvatarData avatar_data;
        std::string vrc_root;
        {
            std::string osc_path = config.vrc_osc_path.empty()
                ? YipOS::VRCAvatarData::DefaultOSCPath()
                : config.vrc_osc_path;
            if (!osc_path.empty()) {
                avatar_data.Scan(osc_path);
                // Detect active avatar from VRC's LocalAvatarData timestamps
                // OSC path is .../VRChat/VRChat/OSC, parent is VRC root
                namespace fs = std::filesystem;
                // Strip trailing slashes so parent_path goes up one level
                std::string osc_clean = osc_path;
                while (!osc_clean.empty() && (osc_clean.back() == '/' || osc_clean.back() == '\\'))
                    osc_clean.pop_back();
                vrc_root = fs::path(osc_clean).parent_path().string();
                avatar_data.DetectCurrentAvatar(vrc_root);
            } else {
                YipOS::Logger::Info("Avatar OSC path not configured — set in Config tab");
            }
        }
        // Fall back to saved current avatar if detection didn't find one
        if (avatar_data.GetCurrentAvatarId().empty()) {
            std::string saved_avatar = config.GetState("avtr.current");
            if (!saved_avatar.empty()) {
                avatar_data.SetCurrentAvatarId(saved_avatar);
                // Load expression params for the saved avatar too
                if (!vrc_root.empty()) {
                    avatar_data.LoadExpressionParams(vrc_root, saved_avatar);
                }
            }
        }
        pda.SetAvatarData(&avatar_data);
        pda.SetOSCManager(&osc);

        // CC (Closed Captions) — audio capture + whisper
        auto audio_capture = YipOS::AudioCapture::Create();
        YipOS::WhisperWorker whisper_worker;
        pda.SetAudioCapture(audio_capture.get());
        pda.SetWhisperWorker(&whisper_worker);

        // Restore persisted state
        std::string saved_disk = config.GetState("stats.disk");
        if (!saved_disk.empty()) {
            pda.GetSystemStats().SetDisk(saved_disk);
        }

        // Wire up OSC input handler
        osc.SetInputHandler([&pda](const std::string& address, float value) {
            if (address.find("CRT_Wrist_") != std::string::npos) {
                pda.OnCRTInput(address, value);
            }
            // Heart rate from VRCOSC / PulsoidToOSC / HRtoVRChat_OSC
            {
                auto pos = address.rfind('/');
                if (pos != std::string::npos) {
                    std::string param = address.substr(pos + 1);
                    // Int params (0-255 raw BPM)
                    if (param == "HR" || param == "Heartrate3" || param == "HeartRateInt") {
                        int bpm = static_cast<int>(value);
                        if (bpm > 0 && bpm < 256) pda.SetHeartRate(bpm);
                    }
                    // Float params (-1.0 to 1.0, maps to 0-255 BPM)
                    else if (param == "Heartrate" || param == "HeartRateFloat" || param == "FullHRPercent") {
                        int bpm = static_cast<int>((value + 1.0f) * 127.5f);
                        if (bpm > 0 && bpm < 256) pda.SetHeartRate(bpm);
                    }
                }
            }
            // BFI (BrainFlowsIntoVRChat) params
            {
                auto bfi_pos = address.find("BFI/");
                if (bfi_pos != std::string::npos) {
                    std::string suffix = address.substr(bfi_pos + 4); // after "BFI/"
                    for (int i = 0; i < YipOS::PDAController::BFI_PARAM_COUNT; i++) {
                        if (suffix == YipOS::PDAController::BFI_PARAMS[i].osc_suffix) {
                            pda.SetBFIParam(i, value);
                            break;
                        }
                    }
                }
            }
            // SPVR device status updates
            if (address.find("SPVR_") != std::string::npos && address.find("_Status") != std::string::npos) {
                int status = static_cast<int>(value);
                for (int i = 0; i < YipOS::PDAController::SPVR_DEVICE_COUNT; i++) {
                    if (address.find(YipOS::PDAController::SPVR_DEVICE_NAMES[i]) != std::string::npos) {
                        pda.SetSPVRStatus(i, status);
                        YipOS::Logger::Info("SPVR status: " + std::string(YipOS::PDAController::SPVR_DEVICE_NAMES[i]) +
                                           " = " + std::to_string(status));
                        break;
                    }
                }
            }
        });

        // UI
        YipOS::UIManager ui;
        // Restore saved window size
        {
            std::string sw = config.GetState("ui.width");
            std::string sh = config.GetState("ui.height");
            if (!sw.empty() && !sh.empty()) {
                ui.SetInitialSize(std::stoi(sw), std::stoi(sh));
            }
        }
        // Resolve assets path relative to executable
        {
            namespace fs = std::filesystem;
            fs::path exe_dir = fs::path(argv[0]).parent_path();
            if (exe_dir.empty()) exe_dir = ".";
            // Try: exe_dir/../assets (build from yip_os/build_win) then exe_dir/assets
            fs::path assets = exe_dir / ".." / "assets";
            if (!fs::exists(assets)) assets = exe_dir / "assets";
            if (!fs::exists(assets)) assets = "assets";
            ui.SetAssetsPath(assets.string());
        }
        if (!ui.Initialize("YipOS")) {
            YipOS::Logger::Critical("Failed to initialize UI");
            osc.Shutdown();
            YipOS::Logger::Shutdown();
            return 1;
        }

        ui.SetConfigPath(configPath);

        // Initialize display with optional boot animation
        InitDisplay(display, pda, ui, config, osc);

        YipOS::Logger::Info("Entering main loop");
        double last_clock = 0;

        while (!ui.ShouldClose() && g_running) {
            // Handle reboot request from UI
            if (pda.ConsumeRebootRequest()) {
                YipOS::Logger::Info("Reboot requested");
                pda.GoHome();
                InitDisplay(display, pda, ui, config, osc);
                last_clock = 0;
                continue;
            }

            // 1. Input (highest priority)
            pda.ProcessInput();

            // 2. Try to drain one buffered write (non-blocking)
            pda.TickRefresh();

            // 3. Clock update (1Hz) — only when buffer is idle
            if (!pda.IsBufferActive()) {
                double now = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (now - last_clock >= 1.0) {
                    pda.UpdateClock();
                    pda.ToggleCursor();
                    net_tracker.Sample();
                    last_clock = now;
                }

                // 4. Screen-specific delta updates
                pda.MaybeUpdate();

                // 5. Maybe start a new background refresh
                pda.MaybeRefresh();
            }

            // 6. Render ImGui
            ui.BeginFrame();
            ui.Render(pda, config, osc);
            ui.EndFrame();

            // 7. Yield — vsync handles frame pacing, brief sleep as fallback
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }

        // Shutdown
        YipOS::Logger::Info("Shutting down");
        ui.SaveWindowSize(config);
        whisper_worker.Stop();
        audio_capture->Stop();
        ui.Shutdown();
        osc.Shutdown();
        config.SaveToFile(configPath);
        YipOS::Logger::Info("YipOS exiting normally");
        YipOS::Logger::Shutdown();
        return 0;
    }
    catch (const std::exception& e) {
        if (YipOS::Logger::IsInitialized()) {
            YipOS::Logger::Critical("Fatal: " + std::string(e.what()));
            YipOS::Logger::Shutdown();
        } else {
            std::cerr << "Fatal: " << e.what() << std::endl;
        }
        return 1;
    }
}
