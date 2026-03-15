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
        });

        // UI
        YipOS::UIManager ui;
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
