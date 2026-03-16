#include "PDAController.hpp"
#include "PDADisplay.hpp"
#include "screens/Screen.hpp"
#include "screens/HomeScreen.hpp"
#include "net/NetTracker.hpp"
#include "platform/SystemStats.hpp"
#include "core/Glyphs.hpp"
#include "core/Config.hpp"
#include "core/Logger.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <thread>
#include <random>

namespace YipOS {

using namespace Glyphs;

static double MonotonicNow() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

PDAController::PDAController(PDADisplay& display, NetTracker& net_tracker, Config& config, float refresh_interval)
    : display_(display), net_tracker_(net_tracker), config_(config),
      system_stats_(SystemStats::Create()),
      refresh_interval_(refresh_interval) {
    // Default SPVR devices to unlocked (1) — matches in-game initial state
    for (auto& s : spvr_status_) s.store(1);

    // Push home screen as root
    auto home = std::make_unique<HomeScreen>(*this);
    screen_stack_.push_back(std::move(home));
}

PDAController::~PDAController() = default;

Screen* PDAController::GetCurrentScreen() const {
    return screen_stack_.empty() ? nullptr : screen_stack_.back().get();
}

char PDAController::GetSpinnerChar() const {
    return SPINNER[cursor_frame_ % 4];
}

std::string PDAController::GetLastInput() const {
    return last_input_;
}

void PDAController::ResetRefresh() {
    last_refresh_ = MonotonicNow();
}

float PDAController::GetRefreshInterval() const {
    Screen* s = GetCurrentScreen();
    if (s && s->refresh_interval > 0) return s->refresh_interval;
    return refresh_interval_;
}

void PDAController::StartRender(Screen* screen) {
    display_.CancelBuffered();
    display_.ClearScreen();

    if (screen->macro_index >= 0) {
        display_.SetMacroMode();
        display_.StampMacro(screen->macro_index);
        display_.SetTextMode();
        display_.BeginBuffered();
        screen->RenderDynamic();
    } else {
        display_.SetTextMode();
        display_.BeginBuffered();
        screen->Render();
    }

    int n = display_.BufferedRemaining();
    Logger::Info("Queued " + std::to_string(n) + " writes for " + screen->name);
    ResetRefresh();
}

void PDAController::PushScreen(const std::string& screen_name) {
    auto screen = CreateScreen(screen_name, *this);
    if (!screen) {
        Logger::Warning("No screen for '" + screen_name + "'");
        return;
    }
    Logger::Info("Push: " + screen->name);
    Screen* raw = screen.get();
    screen_stack_.push_back(std::move(screen));
    StartRender(raw);
}

void PDAController::PopScreen() {
    display_.CancelBuffered();
    if (screen_stack_.size() <= 1) {
        GoHome();
        return;
    }
    auto old = std::move(screen_stack_.back());
    screen_stack_.pop_back();
    Logger::Info("Pop: " + old->name + " -> " + GetCurrentScreen()->name);
    StartRender(GetCurrentScreen());
}

void PDAController::GoHome() {
    display_.CancelBuffered();
    screen_stack_.clear();
    auto home = std::make_unique<HomeScreen>(*this);
    Screen* raw = home.get();
    screen_stack_.push_back(std::move(home));
    StartRender(raw);
}

void PDAController::SetPendingNavigate(const std::string& label) {
    pending_navigate_ = label;
    navigate_time_ = MonotonicNow() + NAVIGATE_DELAY;
}

void PDAController::OnCRTInput(const std::string& address, float value) {
    if (value == 0.0f) return;

    // Extract param name from address
    auto pos = address.rfind('/');
    if (pos == std::string::npos) return;
    std::string param = address.substr(pos + 1);

    double now_ms = MonotonicNow() * 1000.0;
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        auto it = last_trigger_.find(param);
        if (it != last_trigger_.end() && now_ms - it->second < DEBOUNCE_MS) {
            return;
        }
        last_trigger_[param] = now_ms;
        input_queue_.push(param);
    }
    Logger::Debug("OSC Input: " + param);
}

void PDAController::QueueInput(const std::string& param_suffix) {
    std::lock_guard<std::mutex> lock(input_mutex_);
    input_queue_.push("CRT_Wrist_" + param_suffix);
}

void PDAController::ProcessInput() {
    // Handle pending navigation
    if (!pending_navigate_.empty()) {
        if (MonotonicNow() >= navigate_time_) {
            std::string label = pending_navigate_;
            pending_navigate_.clear();
            PushScreen(label);
        }
        return;
    }

    // Drain input queue
    while (true) {
        std::string param;
        {
            std::lock_guard<std::mutex> lock(input_mutex_);
            if (input_queue_.empty()) break;
            param = input_queue_.front();
            input_queue_.pop();
        }

        last_input_ = param;
        last_input_time_ = MonotonicNow();

        // Strip CRT_Wrist_ prefix
        std::string key = param;
        const std::string prefix = "CRT_Wrist_";
        if (key.compare(0, prefix.size(), prefix) == 0) {
            key = key.substr(prefix.size());
        }

        // TL = back (pop screen stack toward home)
        if (key == "TL") {
            Logger::Info("TL: Back");
            PopScreen();
            continue;
        }

        // Route to current screen (ML/BL/touch handled per-screen)
        Screen* current = GetCurrentScreen();
        Logger::Debug("Routing '" + key + "' to screen: " +
                      (current ? current->name : "NONE") +
                      " (stack depth=" + std::to_string(screen_stack_.size()) + ")");
        if (current) {
            bool handled = current->OnInput(key);

            // Check if home screen set pending navigation
            if (!pending_navigate_.empty()) {
                navigate_time_ = MonotonicNow() + NAVIGATE_DELAY;
                return;
            }

            if (handled) continue;
        }

        Logger::Debug("Unhandled input: " + key);
    }
}

bool PDAController::IsBufferActive() const {
    return display_.IsBuffered();
}

bool PDAController::TickRefresh() {
    if (display_.IsBuffered()) {
        // Yield to input if anything queued
        {
            std::lock_guard<std::mutex> lock(input_mutex_);
            if (!input_queue_.empty()) return false;
        }
        return display_.FlushOne();
    }
    return false;
}

void PDAController::MaybeUpdate() {
    if (display_.IsBuffered()) return;
    Screen* s = GetCurrentScreen();
    if (!s || s->update_interval <= 0) return;
    double now = MonotonicNow();
    if (now - last_update_ >= s->update_interval) {
        s->Update();
        last_update_ = now;
    }
}

void PDAController::MaybeRefresh() {
    if (display_.IsBuffered()) return;
    Screen* screen = GetCurrentScreen();
    if (!screen) return;
    // Screens that manage their own updates (e.g. CC) skip the macro re-stamp cycle
    if (screen->skip_clock) return;
    float interval = GetRefreshInterval();
    if (interval <= 0) return;
    double now = MonotonicNow();
    if (now - last_refresh_ >= interval) {
        Logger::Debug("Refreshing " + screen->name);

        if (screen->macro_index >= 0) {
            display_.SetMacroMode();
            display_.StampMacro(screen->macro_index);
            display_.SetTextMode();
            display_.BeginBuffered();
            screen->RenderDynamic();
        } else {
            display_.BeginBuffered();
            screen->Render();
        }

        int n = display_.BufferedRemaining();
        Logger::Debug("Refresh: " + std::to_string(n) + " writes queued");
        last_refresh_ = now;
    }
}

void PDAController::UpdateClock() {
    // Refresh system stats at 1Hz (CPU needs two samples to compute delta)
    system_stats_->Update();

    // Skip clock/cursor writes on screens that manage their own updates
    // (e.g. CC screen uses the full display for rolling text)
    Screen* s = GetCurrentScreen();
    if (s && s->skip_clock) return;

    // Buffer clock + cursor writes so TickRefresh drains them
    // with UI renders between each, instead of blocking ~630ms.
    display_.BeginBuffered();

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time), "%H:%M:%S");
    std::string clock_str = ss.str();
    int col = COLS - 1 - static_cast<int>(clock_str.size());
    display_.WriteText(col, 7, clock_str);
}

void PDAController::ToggleCursor() {
    Screen* s = GetCurrentScreen();
    if (s && s->skip_clock) return;

    cursor_frame_ = (cursor_frame_ + 1) % 4;
    // Goes into the buffer started by UpdateClock
    display_.WriteChar(1, 7, static_cast<int>(GetSpinnerChar()));
}

void PDAController::RunBootSequence() {
    booting_ = true;
    Logger::Info("Boot sequence starting");

    // Ensure no buffered session interferes — boot needs immediate writes
    display_.CancelBuffered();

    // Stamp the boot macro glyph (all static content in 1 write)
    display_.SetMacroMode();
    display_.StampMacro(BOOT_MACRO_INDEX);
    display_.SetTextMode();

    // Progress bar dimensions (must match generate_macro_atlas.py layout_boot)
    constexpr int bar_width = 20;
    constexpr int bar_col = (COLS - bar_width) / 2;

    // Animate: fill bar left to right with variable pacing
    float speed = config_.boot_speed;
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> step_dist(0, 5);
    std::uniform_real_distribution<float> pause_dist(0.1f * speed, 0.4f * speed);
    std::uniform_real_distribution<float> stall_dist(0.6f * speed, 1.2f * speed);
    std::uniform_real_distribution<float> stall_chance(0.0f, 1.0f);

    static constexpr int steps[] = {1, 1, 1, 2, 2, 3};
    int filled = 0;
    while (filled < bar_width) {
        int step = steps[step_dist(rng)];
        step = std::min(step, bar_width - filled);
        for (int i = 0; i < step; i++) {
            display_.WriteGlyph(bar_col + filled, 5, Glyphs::G_SOLID);
            filled++;
        }
        float pause = pause_dist(rng);
        if (stall_chance(rng) < 0.15f) {
            pause = stall_dist(rng);
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(pause * 1000)));
    }

    Logger::Info("Boot sequence complete");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    booting_ = false;
}

void PDAController::Reboot() {
    reboot_requested_ = true;
}

void PDAController::SetSPVRStatus(int device_index, int status) {
    if (device_index >= 0 && device_index < SPVR_DEVICE_COUNT) {
        spvr_status_[device_index].store(status);
    }
}

int PDAController::GetSPVRStatus(int device_index) const {
    if (device_index >= 0 && device_index < SPVR_DEVICE_COUNT) {
        return spvr_status_[device_index].load();
    }
    return 0;
}

} // namespace YipOS
