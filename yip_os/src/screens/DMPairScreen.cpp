#include "DMPairScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "net/DMClient.hpp"
#include "img/QRGen.hpp"
#include "platform/ScreenCapture.hpp"
#include "core/Logger.hpp"
#include <quirc/quirc.h>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace YipOS {

using namespace Glyphs;

static double MonotonicNow() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

DMPairScreen::DMPairScreen(PDAController& pda) : Screen(pda) {
    name = "DM_PAIR";
    macro_index = -1;
    handle_back = true;
    update_interval = 1.0f;
}

DMPairScreen::~DMPairScreen() {
    StopScanning();
    if (qr_rendering_) {
        display_.CancelBuffered();
        display_.SetWriteDelay(saved_write_delay_);
        display_.SetTextMode();
    }
}

void DMPairScreen::Render() {
    if (mode_ == Mode::RENDERING_QR) {
        StartQRRender();
    } else {
        RenderFrame("DM PAIR");
        RenderContent();
        RenderStatusBar();
    }
}

void DMPairScreen::RenderDynamic() {
    if (mode_ == Mode::RENDERING_QR) return;
    RenderContent();
    RenderClock();
    RenderCursor();
}

void DMPairScreen::RenderContent() {
    auto& d = display_;

    switch (mode_) {
    case Mode::CHOOSE:
        d.WriteText(2, 2, "Pair with a friend");
        {
            std::string label = "DIAL";
            for (int i = 0; i < static_cast<int>(label.size()); i++)
                d.WriteChar(2 + i, 4, static_cast<int>(label[i]) + INVERT_OFFSET);
        }
        d.WriteText(8, 4, "Show QR code");
        {
            std::string label = "SCAN";
            for (int i = 0; i < static_cast<int>(label.size()); i++)
                d.WriteChar(2 + i, 5, static_cast<int>(label[i]) + INVERT_OFFSET);
        }
        d.WriteText(8, 5, "Read QR code");
        d.WriteText(2, 6, "JOIN via desktop UI");
        break;

    case Mode::CREATING:
        d.WriteText(2, 3, "Creating session...");
        break;

    case Mode::RENDERING_QR:
        break;  // handled by StartQRRender

    case Mode::SHOW_CODE: {
        d.WriteText(2, 2, "Share this code:");
        d.WriteText(2, 4, "Code: " + code_);
        double remaining = code_expires_ - MonotonicNow();
        if (remaining < 0) remaining = 0;
        int mins = static_cast<int>(remaining) / 60;
        int secs = static_cast<int>(remaining) % 60;
        char timer[16];
        std::snprintf(timer, sizeof(timer), "Expires %d:%02d", mins, secs);
        d.WriteText(2, 5, timer);
        d.WriteText(2, 6, "Waiting for peer...");
        break;
    }

    case Mode::SCANNING:
        d.WriteText(2, 2, "Scanning for QR...");
        d.WriteText(2, 4, "Look at friend's CRT");
        {
            std::string label = "STOP";
            for (int i = 0; i < static_cast<int>(label.size()); i++)
                d.WriteChar(2 + i, 6, static_cast<int>(label[i]) + INVERT_OFFSET);
        }
        break;

    case Mode::JOINED:
        d.WriteText(2, 2, "Peer connected!");
        d.WriteText(2, 3, peer_name_);
        {
            std::string label = "OK";
            for (int i = 0; i < static_cast<int>(label.size()); i++)
                d.WriteChar(2 + i, 5, static_cast<int>(label[i]) + INVERT_OFFSET);
        }
        d.WriteText(6, 5, "Confirm pairing");
        break;

    case Mode::JOINING:
        d.WriteText(2, 3, "Joining session...");
        break;

    case Mode::COMPLETE:
        d.WriteText(2, 2, "Paired with:");
        d.WriteText(2, 3, peer_name_);
        d.WriteText(2, 5, "Press BACK to return");
        break;

    case Mode::FAILED:
        d.WriteText(2, 2, "Pairing failed");
        if (!error_.empty()) {
            std::string err = error_;
            if (static_cast<int>(err.size()) > 36)
                err = err.substr(0, 33) + "...";
            d.WriteText(2, 3, err);
        }
        {
            std::string label = "RETRY";
            for (int i = 0; i < static_cast<int>(label.size()); i++)
                d.WriteChar(2 + i, 5, static_cast<int>(label[i]) + INVERT_OFFSET);
        }
        break;
    }
}

void DMPairScreen::StartQRRender() {
    saved_write_delay_ = display_.GetWriteDelay();

    // Encode the pairing code as QR
    QRGen qr;
    if (!qr.Encode(code_)) {
        Logger::Warning("DMPair: QR encode failed for code " + code_);
        mode_ = Mode::SHOW_CODE;
        pda_.StartRender(this);
        return;
    }

    // Clear + stamp template + write data modules (same as QRTestScreen)
    display_.ClearScreen();
    display_.SetMacroMode();
    display_.StampMacro(QR_TEMPLATE_MACRO);

    display_.SetBitmapMode();
    display_.SetWriteDelay(0.07f);  // SLOW mode
    display_.BeginBuffered();

    auto& modules = qr.GetLightModules();
    for (auto& mod : modules) {
        display_.WriteChar(mod.col, mod.row, 255);  // VQ_WHITE
    }

    qr_rendering_ = true;
    Logger::Info("DMPair: QR render started (" +
                 std::to_string(modules.size()) + " data writes)");
}

void DMPairScreen::StartScanning() {
    if (scan_running_) return;

    screen_capture_ = ScreenCapture::Create();
    scan_running_ = true;
    scanned_code_.clear();

    scan_thread_ = std::make_unique<std::thread>([this]() {
        struct quirc* q = quirc_new();
        if (!q) {
            Logger::Warning("DMPair: quirc_new failed");
            scan_running_ = false;
            return;
        }

        while (scan_running_) {
            Screenshot shot;
            if (!screen_capture_->Capture(shot) || shot.width == 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(SCAN_INTERVAL * 1000)));
                continue;
            }

            if (quirc_resize(q, shot.width, shot.height) < 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(SCAN_INTERVAL * 1000)));
                continue;
            }

            // Copy grayscale image to quirc buffer
            int w, h;
            uint8_t* buf = quirc_begin(q, &w, &h);
            int copy_w = std::min(w, shot.width);
            int copy_h = std::min(h, shot.height);
            for (int y = 0; y < copy_h; y++) {
                std::memcpy(buf + y * w, shot.pixels.data() + y * shot.width, copy_w);
            }
            quirc_end(q);

            // Check decoded results
            int count = quirc_count(q);
            for (int i = 0; i < count; i++) {
                struct quirc_code qc;
                struct quirc_data qd;
                quirc_extract(q, i, &qc);
                if (quirc_decode(&qc, &qd) == QUIRC_SUCCESS) {
                    std::string payload(reinterpret_cast<const char*>(qd.payload),
                                        qd.payload_len);
                    // Check if it looks like a 6-digit pairing code
                    if (payload.size() == 6) {
                        bool all_digits = true;
                        for (char c : payload) {
                            if (c < '0' || c > '9') { all_digits = false; break; }
                        }
                        if (all_digits) {
                            std::lock_guard<std::mutex> lock(scan_result_mutex_);
                            scanned_code_ = payload;
                            Logger::Info("DMPair: scanned QR code: " + payload);
                            scan_running_ = false;
                            quirc_destroy(q);
                            return;
                        }
                    }
                }
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(SCAN_INTERVAL * 1000)));
        }

        quirc_destroy(q);
    });
}

void DMPairScreen::StopScanning() {
    scan_running_ = false;
    if (scan_thread_ && scan_thread_->joinable()) {
        scan_thread_->join();
    }
    scan_thread_.reset();
}

void DMPairScreen::Update() {
    auto& client = pda_.GetDMClient();

    // Check if QR render is complete → switch to SHOW_CODE
    if (mode_ == Mode::RENDERING_QR && qr_rendering_) {
        if (!display_.IsBuffered()) {
            qr_rendering_ = false;
            display_.SetWriteDelay(saved_write_delay_);
            // Stay in bitmap mode showing the QR — switch to SHOW_CODE
            // for status polling (but don't re-render the screen)
            mode_ = Mode::SHOW_CODE;
            Logger::Info("DMPair: QR render complete, polling for peer");
        }
    }

    // Poll for peer join (initiator)
    if (mode_ == Mode::SHOW_CODE) {
        if (MonotonicNow() > code_expires_) {
            display_.SetTextMode();
            mode_ = Mode::FAILED;
            error_ = "Code expired";
            pda_.StartRender(this);
            return;
        }

        double now = MonotonicNow();
        if (now - last_poll_ >= POLL_INTERVAL) {
            last_poll_ = now;
            std::string status, peer;
            if (client.PairStatus(session_id_, status, peer)) {
                if (status == "joined" || status == "confirmed") {
                    peer_name_ = peer;
                    display_.SetTextMode();
                    mode_ = Mode::JOINED;
                    pda_.StartRender(this);
                }
            }
        }
    }

    // Check if scanner found a QR code
    if (mode_ == Mode::SCANNING) {
        std::string code;
        {
            std::lock_guard<std::mutex> lock(scan_result_mutex_);
            code = scanned_code_;
        }
        if (!code.empty()) {
            StopScanning();
            // Auto-join with scanned code
            mode_ = Mode::JOINING;
            pda_.StartRender(this);

            std::string sid, peer;
            if (client.PairJoin(code, sid, peer)) {
                if (client.PairConfirm(sid)) {
                    session_id_ = sid;
                    peer_name_ = peer;
                    client.AddSession(sid, "", peer);
                    pda_.SaveDMSessions();
                    mode_ = Mode::COMPLETE;
                } else {
                    mode_ = Mode::FAILED;
                    error_ = "Confirm failed";
                }
            } else {
                mode_ = Mode::FAILED;
                error_ = "Invalid or expired code";
            }
            pda_.StartRender(this);
        }
    }

    // Check if ImGui-initiated join completed
    if (mode_ == Mode::CHOOSE || mode_ == Mode::JOINING) {
        auto& info = client.GetPairInfo();
        if (info.state == PairState::COMPLETE) {
            session_id_ = info.session_id;
            peer_name_ = info.peer_name;
            mode_ = Mode::COMPLETE;
            client.AddSession(session_id_, "", peer_name_);
            pda_.SaveDMSessions();
            info.state = PairState::IDLE;
            pda_.StartRender(this);
        } else if (info.state == PairState::JOINED) {
            session_id_ = info.session_id;
            peer_name_ = info.peer_name;
            mode_ = Mode::JOINED;
            info.state = PairState::IDLE;
            pda_.StartRender(this);
        } else if (info.state == PairState::FAILED) {
            error_ = info.error;
            mode_ = Mode::FAILED;
            info.state = PairState::IDLE;
            pda_.StartRender(this);
        }
    }
}

bool DMPairScreen::OnInput(const std::string& key) {
    auto& client = pda_.GetDMClient();

    if (mode_ == Mode::CHOOSE) {
        // DIAL touch (row 4) — create session + render QR
        if (key == "41") {
            mode_ = Mode::CREATING;
            pda_.StartRender(this);

            std::string code, sid;
            if (client.PairCreate(code, sid)) {
                code_ = code;
                session_id_ = sid;
                code_expires_ = MonotonicNow() + CODE_TTL;
                last_poll_ = MonotonicNow();
                mode_ = Mode::RENDERING_QR;
            } else {
                mode_ = Mode::FAILED;
                error_ = "Network error";
            }
            pda_.StartRender(this);
            return true;
        }
        // SCAN touch (row 5) — start screen capture scanning
        if (key == "46") {
            mode_ = Mode::SCANNING;
            StartScanning();
            pda_.StartRender(this);
            return true;
        }
    }

    if (mode_ == Mode::SCANNING) {
        // STOP touch
        if (key == "46" || key == "41") {
            StopScanning();
            mode_ = Mode::CHOOSE;
            pda_.StartRender(this);
            return true;
        }
    }

    if (mode_ == Mode::JOINED) {
        if (key == "41" || key == "53") {
            if (client.PairConfirm(session_id_)) {
                client.AddSession(session_id_, "", peer_name_);
                pda_.SaveDMSessions();
                mode_ = Mode::COMPLETE;
            } else {
                mode_ = Mode::FAILED;
                error_ = "Confirm failed";
            }
            pda_.StartRender(this);
            return true;
        }
    }

    if (mode_ == Mode::FAILED) {
        if (key == "41" || key == "53") {
            mode_ = Mode::CHOOSE;
            error_.clear();
            pda_.StartRender(this);
            return true;
        }
    }

    // While QR is rendering, consume input to prevent navigation
    if (mode_ == Mode::RENDERING_QR || mode_ == Mode::SHOW_CODE) {
        if (key == "TL" || key == "ML") {
            // Cancel QR render and go back
            if (qr_rendering_) {
                display_.CancelBuffered();
                display_.SetWriteDelay(saved_write_delay_);
                qr_rendering_ = false;
            }
            display_.SetTextMode();
            mode_ = Mode::CHOOSE;
            pda_.StartRender(this);
            return true;
        }
        return true;  // consume all other input
    }

    return false;
}

} // namespace YipOS
