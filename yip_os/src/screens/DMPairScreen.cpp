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
    macro_index = 40;
    handle_back = true;  // we handle TL ourselves in OnInput
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
    } else if (mode_ == Mode::SHOW_CODE) {
        // Don't do a full render — the QR is on screen.
        // Update() manages the periodic QR + code overlay refresh.
    } else {
        RenderFrame("DM PAIR");
        RenderContent();
        RenderStatusBar();
    }
}

void DMPairScreen::RenderDynamic() {
    if (mode_ == Mode::RENDERING_QR || mode_ == Mode::SHOW_CODE) return;
    RenderContent();
    RenderClock();
    RenderCursor();
}

void DMPairScreen::RenderContent() {
    auto& d = display_;

    switch (mode_) {
    case Mode::CHOOSE:
        break;  // all static content is in macro 40

    case Mode::CREATING:
        d.WriteText(2, 3, "Creating session...");
        break;

    case Mode::RENDERING_QR:
        break;  // handled by StartQRRender

    case Mode::SHOW_CODE:
        break;  // handled by WriteCodeOverlay on the QR screen

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
        mode_ = Mode::FAILED;
        error_ = "QR encode failed";
        RequestRender();
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
    skip_clock = true;
    Logger::Info("DMPair: QR render started (" +
                 std::to_string(modules.size()) + " data writes)");
}

void DMPairScreen::RequestRender() {
    // Set macro_index based on current mode so StartRender does the right thing:
    // CHOOSE uses macro 40 (static layout), all other modes use -1 (dynamic Render)
    macro_index = (mode_ == Mode::CHOOSE) ? CHOOSE_MACRO : -1;
    RequestRender();
}

void DMPairScreen::WriteCodeOverlay() {
    // Write the 6-digit code at text row 7, col 14 (right after baked "Manual code:")
    // This is called repeatedly so dropped OSC writes get retried
    display_.WriteText(14, 7, code_);
}

void DMPairScreen::StartScanning() {
    if (scan_running_) return;

    screen_capture_ = ScreenCapture::Create();
    scan_running_ = true;
    scanned_code_.clear();

    scan_thread_ = std::make_unique<std::thread>([this]() {
        Logger::Info("DMPair: scan thread started");

        struct quirc* q = quirc_new();
        if (!q) {
            Logger::Warning("DMPair: quirc_new failed");
            scan_running_ = false;
            return;
        }

        int scan_count = 0;
        while (scan_running_) {
            Screenshot shot;
            if (!screen_capture_->Capture(shot) || shot.width == 0) {
                Logger::Debug("DMPair: capture failed or empty frame");
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(static_cast<int>(SCAN_INTERVAL * 1000)));
                continue;
            }

            scan_count++;
            Logger::Debug("DMPair: captured frame " + std::to_string(scan_count) +
                          " (" + std::to_string(shot.width) + "x" +
                          std::to_string(shot.height) + ")");

            if (quirc_resize(q, shot.width, shot.height) < 0) {
                Logger::Warning("DMPair: quirc_resize failed");
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
            if (count > 0)
                Logger::Info("DMPair: quirc found " + std::to_string(count) + " QR candidate(s)");
            for (int i = 0; i < count; i++) {
                struct quirc_code qc;
                struct quirc_data qd;
                quirc_extract(q, i, &qc);
                quirc_decode_error_t err = quirc_decode(&qc, &qd);
                if (err == QUIRC_SUCCESS) {
                    std::string payload(reinterpret_cast<const char*>(qd.payload),
                                        qd.payload_len);
                    Logger::Info("DMPair: decoded QR payload: \"" + payload + "\"");
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
                } else {
                    Logger::Debug("DMPair: quirc decode error: " +
                                  std::string(quirc_strerror(err)));
                }
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(static_cast<int>(SCAN_INTERVAL * 1000)));
        }

        Logger::Info("DMPair: scan thread stopped");
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

    // Check if QR render is complete → switch to text mode for code overlay
    if (mode_ == Mode::RENDERING_QR && qr_rendering_) {
        if (!display_.IsBuffered()) {
            qr_rendering_ = false;
            display_.SetWriteDelay(saved_write_delay_);
            // Switch to text mode and write the code overlay immediately
            // (the QR bitmap stays on the render texture)
            display_.SetTextMode();
            WriteCodeOverlay();
            last_qr_refresh_ = MonotonicNow();
            mode_ = Mode::SHOW_CODE;
            Logger::Info("DMPair: QR render complete, code overlay written");
        }
    }

    // Poll for peer join + refresh QR display
    if (mode_ == Mode::SHOW_CODE) {
        if (MonotonicNow() > code_expires_) {
            skip_clock = false;
            mode_ = Mode::FAILED;
            error_ = "Code expired";
            RequestRender();
            return;
        }

        double now = MonotonicNow();

        // Poll backend for peer join
        if (now - last_poll_ >= POLL_INTERVAL) {
            last_poll_ = now;
            std::string status, peer;
            if (client.PairStatus(session_id_, status, peer)) {
                if (status == "joined" || status == "confirmed") {
                    peer_name_ = peer;
                    skip_clock = false;
                    mode_ = Mode::JOINED;
                    RequestRender();
                    return;
                }
            }
        }

        // After bitmap refresh completes, switch back to text mode for overlay
        if (qr_needs_text_overlay_ && !display_.IsBuffered()) {
            display_.SetTextMode();
            WriteCodeOverlay();
            qr_needs_text_overlay_ = false;
        }

        // Re-send QR data + code overlay when display is idle.
        // This patches any dropped OSC writes without clearing the screen.
        if (!qr_needs_text_overlay_ && !display_.IsBuffered() &&
            now - last_qr_refresh_ >= QR_REFRESH_INTERVAL) {
            last_qr_refresh_ = now;

            // Re-send QR light modules in bitmap mode
            display_.SetBitmapMode();
            display_.BeginBuffered();
            QRGen qr;
            if (qr.Encode(code_)) {
                for (auto& mod : qr.GetLightModules()) {
                    display_.WriteChar(mod.col, mod.row, 255);  // VQ_WHITE
                }
            }

            // After these writes flush, we'll switch to text and write the code
            qr_needs_text_overlay_ = true;
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
            RequestRender();

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
            RequestRender();
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
            RequestRender();
        } else if (info.state == PairState::JOINED) {
            session_id_ = info.session_id;
            peer_name_ = info.peer_name;
            mode_ = Mode::JOINED;
            info.state = PairState::IDLE;
            RequestRender();
        } else if (info.state == PairState::FAILED) {
            error_ = info.error;
            mode_ = Mode::FAILED;
            info.state = PairState::IDLE;
            RequestRender();
        }
    }
}

bool DMPairScreen::OnInput(const std::string& key) {
    auto& client = pda_.GetDMClient();

    // Back button — handle in every mode
    if (key == "TL") {
        if (mode_ == Mode::RENDERING_QR || mode_ == Mode::SHOW_CODE) {
            // Cancel QR render and return to CHOOSE
            if (qr_rendering_) {
                display_.CancelBuffered();
                display_.SetWriteDelay(saved_write_delay_);
                qr_rendering_ = false;
            }
            display_.SetTextMode();
            skip_clock = false;
            mode_ = Mode::CHOOSE;
            RequestRender();
            return true;
        }
        if (mode_ == Mode::SCANNING) {
            StopScanning();
        }
        // Pop screen — tell controller we're done handling back
        skip_clock = false;
        handle_back = false;
        return false;  // controller will pop
    }

    if (mode_ == Mode::CHOOSE) {
        // DIAL touch — contact 12 (col 1, row 2, near display row 4)
        if (key == "12") {
            mode_ = Mode::CREATING;
            RequestRender();

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
            RequestRender();
            return true;
        }
        // SCAN touch — contact 52 (col 5, row 2, right side)
        if (key == "52") {
            mode_ = Mode::SCANNING;
            StartScanning();
            RequestRender();
            return true;
        }
    }

    if (mode_ == Mode::SCANNING) {
        // STOP touch — either side
        if (key == "12" || key == "52") {
            StopScanning();
            mode_ = Mode::CHOOSE;
            RequestRender();
            return true;
        }
    }

    if (mode_ == Mode::JOINED) {
        // OK button at row 5 — contact 12 or TR confirm
        if (key == "12" || key == "53") {
            if (client.PairConfirm(session_id_)) {
                client.AddSession(session_id_, "", peer_name_);
                pda_.SaveDMSessions();
                mode_ = Mode::COMPLETE;
            } else {
                mode_ = Mode::FAILED;
                error_ = "Confirm failed";
            }
            RequestRender();
            return true;
        }
    }

    if (mode_ == Mode::FAILED) {
        // RETRY button at row 5 — contact 12 or TR confirm
        if (key == "12" || key == "53") {
            mode_ = Mode::CHOOSE;
            error_.clear();
            RequestRender();
            return true;
        }
    }

    // While QR is rendering/showing, consume all input (TL handled above)
    if (mode_ == Mode::RENDERING_QR || mode_ == Mode::SHOW_CODE) {
        return true;
    }

    return false;
}

} // namespace YipOS
