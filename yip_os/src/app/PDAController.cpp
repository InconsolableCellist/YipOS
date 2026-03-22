#include "PDAController.hpp"
#include "PDADisplay.hpp"
#include "screens/Screen.hpp"
#include "screens/HomeScreen.hpp"
#include "net/NetTracker.hpp"
#include "net/VRCXData.hpp"
#include "net/ChatClient.hpp"
#include "net/StockClient.hpp"
#include "net/TwitchClient.hpp"
#include "media/MediaController.hpp"
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
    last_activity_ = MonotonicNow();

    // Initialize ChatClient
    chat_client_ = std::make_unique<ChatClient>();
    chat_client_->SetEndpoint("https://noisy-sun-1bb7.dan-a7b.workers.dev/messages");
    // Restore last seen timestamp
    std::string last_seen = config_.GetState("chat.last_seen");
    if (!last_seen.empty()) {
        try { chat_client_->SetLastSeenDate(std::stoll(last_seen)); }
        catch (...) {}
    }

    // Initialize media controller
    media_controller_ = MediaController::Create();
    if (media_controller_) media_controller_->Initialize();

    // Initialize stock client (always created, gated by stonk.enabled at fetch time)
    stock_client_ = std::make_unique<StockClient>();
    ReloadStockSymbols();

    // Initialize Twitch client (gated by twitch.enabled + twitch.channel)
    twitch_client_ = std::make_unique<TwitchClient>();
    std::string twitch_channel = config_.GetState("twitch.channel");
    if (!twitch_channel.empty()) {
        twitch_client_->SetChannel(twitch_channel);
        if (config_.GetState("twitch.enabled", "0") == "1") {
            twitch_client_->Connect();
        }
    }

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
    if (s && s->refresh_interval != 0) return s->refresh_interval;  // negative = disabled, positive = override
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
            if (label == "__POP__") {
                PopScreen();
            } else {
                PushScreen(label);
            }
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
        ResetAutolockTimer();

        // Strip CRT_Wrist_ prefix
        std::string key = param;
        const std::string prefix = "CRT_Wrist_";
        if (key.compare(0, prefix.size(), prefix) == 0) {
            key = key.substr(prefix.size());
        }

        // Soft lock: block all input except SEL (TR) x3 to unlock
        if (soft_locked_) {
            if (key == "TR") {
                double now = MonotonicNow();
                if (soft_lock_sel_count_ > 0 &&
                    (now - soft_lock_last_sel_) > SOFT_LOCK_SEL_WINDOW) {
                    soft_lock_sel_count_ = 0;
                }
                soft_lock_sel_count_++;
                soft_lock_last_sel_ = now;
                if (soft_lock_sel_count_ >= SOFT_LOCK_SEL_NEEDED) {
                    soft_locked_ = false;
                    soft_lock_sel_count_ = 0;
                    Logger::Info("Soft lock disengaged");
                    // Clear lock icon immediately
                    display_.CancelBuffered();
                    display_.BeginBuffered();
                    display_.WriteGlyph(2, 7, G_HLINE);
                }
            } else {
                // Wrong button — flash the lock icon and re-render the
                // current screen so VRC display artifacts get cleared.
                // Skip re-render for screens that own the display (e.g. IMG
                // bitmap mode) — a full StartRender would destroy their
                // progress and mode settings.
                lock_flash_until_ = MonotonicNow() + LOCK_FLASH_DURATION;
                Screen* current = GetCurrentScreen();
                if (current && !current->skip_clock) {
                    StartRender(current);
                } else {
                    display_.CancelBuffered();
                    display_.BeginBuffered();
                }
                // Write flashed lock icon on top of the freshly rendered screen
                display_.WriteChar(2, 7, G_LOCK_INV);
            }
            continue;
        }

        // When hard-locked, only route to the lock screen (no TL back)
        if (locked_) {
            Screen* current = GetCurrentScreen();
            if (current) current->OnInput(key);
            continue;
        }

        // TL = back (pop screen stack toward home)
        // Unless the current screen wants to handle it (e.g. IMG display mode)
        if (key == "TL") {
            Screen* current = GetCurrentScreen();
            if (current && current->handle_back) {
                current->OnInput(key);
                continue;
            }
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

        // Clear the render texture first so VRC UI artifacts / garbled
        // pixels are wiped, matching what StartRender() does on entry.
        display_.ClearScreen();

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

    // Periodically check for new notifications (throttled to 30s)
    RefreshNotifCache();

    // Periodically check for new chat messages
    RefreshChatCache();

    // Periodically refresh stock data
    RefreshStockCache();

    // Autolock check — engage soft lock (overlay, not screen push)
    if (!locked_ && !soft_locked_ && !booting_) {
        std::string autolock_str = config_.GetState("lock.autolock", "30");
        int autolock_secs = std::stoi(autolock_str);
        if (autolock_secs > 0 && last_activity_ > 0) {
            double elapsed = MonotonicNow() - last_activity_;
            if (elapsed >= autolock_secs) {
                Logger::Info("Soft lock engaged after " + std::to_string(autolock_secs) + "s");
                soft_locked_ = true;
                soft_lock_sel_count_ = 0;
            }
        }
    }

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

    // Status icons at cols 2-3
    // Col 2: lock indicator
    if (soft_locked_) {
        bool flashing = IsLockFlashing();
        int glyph = flashing ? (G_LOCK_INV) : G_LOCK;
        display_.WriteChar(2, 7, glyph);
    } else {
        display_.WriteGlyph(2, 7, G_HLINE);
    }
    // Col 3: notification indicator (VRCX notifs OR chat unseen)
    if (has_unseen_notifs_ || has_unseen_chat_) {
        display_.WriteGlyph(3, 7, G_BULLET);
    } else {
        display_.WriteGlyph(3, 7, G_HLINE);
    }

    // Per-screen live indicators (written at 1Hz so they update without full refresh)
    if (s) {
        if (s->name == "HOME") {
            // CHAT tile [1][4] — "*" at col 38, row 4 (ZONE_ROWS[1])
            if (has_unseen_chat_) {
                display_.WriteChar(38, ZONE_ROWS[1], static_cast<int>('*') + INVERT_OFFSET);
            } else {
                display_.WriteChar(38, ZONE_ROWS[1], static_cast<int>(' '));
            }
        }
        else if (s->name == "CHAT" && has_unseen_chat_) {
            // "(NEW MSG)" over the top-left border to signal new messages arrived
            display_.WriteText(1, 0, "(NEW MSG)");
        }
    }
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
    ResetAutolockTimer();
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

bool PDAController::HasUnseenNotifs() {
    if (!vrcx_data_ || !vrcx_data_->IsOpen()) return false;
    auto notifs = vrcx_data_->GetNotifications(1);
    if (notifs.empty()) return false;
    std::string last_seen = config_.GetState("notif.last_seen");
    return last_seen != notifs[0].created_at;
}

void PDAController::MarkNotifsSeen() {
    if (!vrcx_data_ || !vrcx_data_->IsOpen()) return;
    auto notifs = vrcx_data_->GetNotifications(1);
    if (!notifs.empty()) {
        config_.SetState("notif.last_seen", notifs[0].created_at);
    }
    has_unseen_notifs_ = false;
}

void PDAController::ClearAllNotifs() {
    // Mark all current notifs as seen
    MarkNotifsSeen();
    has_unseen_notifs_ = false;
}

void PDAController::SetHeartRate(int bpm) {
    hr_bpm_.store(bpm);
    hr_last_update_.store(MonotonicNow());
}

bool PDAController::HasHeartRate() const {
    double last = hr_last_update_.load();
    if (last <= 0) return false;
    return (MonotonicNow() - last) < HR_TIMEOUT;
}

void PDAController::SetBFIParam(int index, float value) {
    if (index >= 0 && index < BFI_PARAM_COUNT) {
        bfi_values_[index].store(value);
        bfi_last_update_.store(MonotonicNow());
    }
}

float PDAController::GetBFIParam(int index) const {
    if (index >= 0 && index < BFI_PARAM_COUNT) {
        return bfi_values_[index].load();
    }
    return 0.0f;
}

bool PDAController::HasBFIData() const {
    double last = bfi_last_update_.load();
    if (last <= 0) return false;
    return (MonotonicNow() - last) < BFI_TIMEOUT;
}

void PDAController::SetLocked(bool locked) {
    locked_ = locked;
}

bool PDAController::IsLockFlashing() const {
    return MonotonicNow() < lock_flash_until_;
}

void PDAController::ResetAutolockTimer() {
    last_activity_ = MonotonicNow();
}

void PDAController::RefreshNotifCache() {
    double now = MonotonicNow();
    if (now - last_notif_check_ < NOTIF_CHECK_INTERVAL) return;
    last_notif_check_ = now;
    has_unseen_notifs_ = HasUnseenNotifs();
}

void PDAController::RefreshChatCache() {
    // Determine interval from NVRAM config
    std::string refresh_str = config_.GetState("chat.refresh", "60");
    double interval = CHAT_CHECK_INTERVAL_DEFAULT;
    try { interval = std::stod(refresh_str); }
    catch (...) {}
    if (interval <= 0) return; // OFF

    // Use shorter interval when CHAT screen is active
    Screen* s = GetCurrentScreen();
    if (s && s->name == "CHAT") {
        interval = 15.0;
    }

    double now = MonotonicNow();
    if (now - last_chat_check_ < interval) return;
    last_chat_check_ = now;

    if (chat_client_->FetchMessages()) {
        has_unseen_chat_ = chat_client_->HasUnseen();
    }
}

void PDAController::SetDroppedImagePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(drop_mutex_);
    dropped_image_path_ = path;
}

std::string PDAController::ConsumeDroppedImagePath() {
    std::lock_guard<std::mutex> lock(drop_mutex_);
    std::string path;
    std::swap(path, dropped_image_path_);
    return path;
}

void PDAController::MarkChatSeen() {
    auto& msgs = chat_client_->GetMessages();
    if (!msgs.empty()) {
        int64_t newest = msgs[0].date;
        chat_client_->MarkAllSeen(newest);
        config_.SetState("chat.last_seen", std::to_string(newest));
    }
    has_unseen_chat_ = false;
}

void PDAController::ReloadStockSymbols() {
    if (!stock_client_) return;
    std::string sym_str = config_.GetState("stonk.symbols", "DOGE,BTC,AAPL,NVDA,GME");
    std::vector<std::string> symbols;
    size_t start = 0;
    while (start < sym_str.size()) {
        size_t end = sym_str.find(',', start);
        if (end == std::string::npos) end = sym_str.size();
        std::string s = sym_str.substr(start, end - start);
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        while (!s.empty() && s.back() == ' ') s.pop_back();
        if (!s.empty()) symbols.push_back(s);
        start = end + 1;
    }
    stock_client_->SetSymbols(symbols);
}

void PDAController::RefreshStockCache() {
    if (!stock_client_) return;
    if (config_.GetState("stonk.enabled", "0") != "1") return;

    // Re-sync symbol list from config each cycle (UI may have changed it)
    ReloadStockSymbols();

    std::string refresh_str = config_.GetState("stonk.refresh", "300");
    double interval = STOCK_CHECK_INTERVAL_DEFAULT;
    try { interval = std::stod(refresh_str); }
    catch (...) {}
    if (interval <= 0) return;

    // Use a much shorter interval when the STONK screen is active,
    // and fetch immediately (interval=0) if no data exists yet.
    Screen* s = GetCurrentScreen();
    bool on_stonk = s && (s->name == "STONK" || s->name == "STONK_LIST");
    if (on_stonk) {
        std::string sel = config_.GetState("stonk.selected", "DOGE");
        auto* quote = stock_client_->GetQuote(sel);
        if (!quote || quote->history.empty()) {
            interval = 0;  // fetch right now — no data yet
        } else {
            interval = 30.0;  // faster refresh while viewing
        }
    }

    double now = MonotonicNow();
    if (interval > 0 && now - last_stock_check_ < interval) return;
    last_stock_check_ = now;

    std::string window = config_.GetState("stonk.window", "1MO");
    stock_client_->FetchAll(window);
}

} // namespace YipOS
