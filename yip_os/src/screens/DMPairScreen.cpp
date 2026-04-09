#include "DMPairScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Config.hpp"
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
    LoadSounds();
}

void DMPairScreen::LoadSounds() {
    std::string assets = pda_.GetAssetsPath();
    if (assets.empty()) return;
    scan_beep_.LoadWAV(assets + "/sounds/scan_beep.wav");
    error_sound_.LoadWAV(assets + "/sounds/error.wav");
}

bool DMPairScreen::AudioEnabled() const {
    return pda_.GetConfig().GetState("dm.audio", "0") == "1";
}

DMPairScreen::~DMPairScreen() {
    scan_beep_.Stop();
    error_sound_.Stop();
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
        // QR is on screen, Update() manages refresh.
    } else if (macro_index < 0) {
        // Transient dynamic modes: draw frame + content manually
        RenderFrame("DM PAIR");
        display_.WriteGlyph(0, 1, G_LEFT_A);
        RenderContent();
        RenderStatusBar();
    }
    // Macro modes (CHOOSE, SCANNING, COMPLETE, FAILED, JOINED):
    // static content handled by macro stamp in StartRender
}

void DMPairScreen::RenderDynamic() {
    if (mode_ == Mode::RENDERING_QR || mode_ == Mode::SHOW_CODE) return;

    // Write only the dynamic parts over the macro stamp
    switch (mode_) {
    case Mode::COMPLETE:
    case Mode::JOINED:
        display_.WriteText(2, 3, peer_name_);
        break;
    case Mode::FAILED:
        if (!error_.empty()) {
            std::string err = error_;
            if (static_cast<int>(err.size()) > 36)
                err = err.substr(0, 33) + "...";
            display_.WriteText(2, 3, err);
        }
        break;
    default:
        break;
    }

    // Transient modes still need full content
    if (mode_ == Mode::CREATING || mode_ == Mode::JOINING)
        RenderContent();

    RenderClock();
    RenderCursor();
}

void DMPairScreen::RenderContent() {
    auto& d = display_;

    switch (mode_) {
    case Mode::CREATING:
        d.WriteText(2, 3, "Creating session...");
        break;
    case Mode::JOINING:
        d.WriteText(2, 3, "Joining session...");
        break;
    default:
        break;  // all other modes use macros
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

    // Clear + stamp template (gives us finders/timing as fast background)
    display_.ClearScreen();
    display_.SetMacroMode();
    display_.StampMacro(QR_TEMPLATE_MACRO);

    // Write the manual-entry code first so it's visible during the slow QR draw
    display_.SetTextMode();
    WriteCodeOverlay();

    display_.SetBitmapMode();
    display_.SetWriteDelay(0.07f);  // SLOW mode
    display_.BeginBuffered();

    // Write ALL modules (dark + light) for the actual payload.  The template's
    // format-info bits correspond to a *different* payload's mask pattern and
    // would otherwise make the QR undecodable — overwriting them here guarantees
    // the QR matches what QRGen::EvaluateMask() chose.
    auto& matrix = qr.GetMatrix();
    constexpr int OFF = QRGen::OFFSET;
    constexpr int SZ = QRGen::SIZE;

    // QR modules only. The template stamp (macro 37) already provides a
    // 5-module light quiet zone around the 21×21 area — don't overwrite it.
    for (int r = 0; r < SZ; r++) {
        for (int c = 0; c < SZ; c++) {
            display_.WriteChar(c + OFF, r + OFF, matrix[r][c] ? 0 : 255);
        }
    }

    qr_rendering_ = true;
    skip_clock = true;
    int writes = SZ * SZ;
    Logger::Info("DMPair: QR render started (" +
                 std::to_string(writes) + " module writes)");
}

void DMPairScreen::RequestRender() {
    switch (mode_) {
    case Mode::CHOOSE:   macro_index = CHOOSE_MACRO; break;
    case Mode::SCANNING: macro_index = SCANNING_MACRO; break;
    case Mode::COMPLETE: macro_index = COMPLETE_MACRO; break;
    case Mode::FAILED:   macro_index = FAILED_MACRO; break;
    case Mode::JOINED:   macro_index = JOINED_MACRO; break;
    default:             macro_index = -1; break;  // CREATING, JOINING, RENDERING_QR
    }
    pda_.StartRender(this);
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

            // Build grayscale variants from captured RGB channels.
            // Different CRT phosphor colors can kill contrast in standard
            // luminance conversion (e.g. red-on-white), so we also try
            // each individual channel.  The channel with the most contrast
            // between QR modules and background will decode successfully.
            std::vector<uint8_t> gray_lum;
            shot.ToGrayscale(gray_lum);
            const std::vector<uint8_t>* gray_modes[] = {
                &gray_lum, &shot.pixels_r, &shot.pixels_g, &shot.pixels_b
            };
            static const char* gray_names[] = { "lum", "R", "G", "B" };

            // Try decoding at multiple vertical scales. The CRT mesh stretches
            // the render texture non-uniformly, so the captured QR may be
            // squashed/stretched.  Rescale along Y and try each pass.
            // For each scale, try all grayscale modes before moving on.
            static constexpr float Y_SCALES[] = { 1.0f, 0.5f, 2.0f, 0.75f, 1.5f };
            std::string found_code;
            for (float y_scale : Y_SCALES) {
                int out_w = shot.width;
                int out_h = std::max(32, static_cast<int>(shot.height * y_scale));

                if (quirc_resize(q, out_w, out_h) < 0) {
                    Logger::Warning("DMPair: quirc_resize failed");
                    continue;
                }

                for (int gm = 0; gm < 4; gm++) {
                    const auto& src_pixels = *gray_modes[gm];

                    int w, h;
                    uint8_t* buf = quirc_begin(q, &w, &h);
                    int copy_w = std::min(w, shot.width);
                    for (int y = 0; y < h; y++) {
                        int src_y = static_cast<int>(y / y_scale);
                        if (src_y >= shot.height) src_y = shot.height - 1;
                        std::memcpy(buf + y * w,
                                    src_pixels.data() + src_y * shot.width, copy_w);
                    }
                    quirc_end(q);

                    int count = quirc_count(q);
                    if (count > 0) {
                        Logger::Debug("DMPair: quirc @y=" + std::to_string(y_scale) +
                                      " ch=" + gray_names[gm] +
                                      " found " + std::to_string(count) + " candidate(s)");
                    }
                    for (int i = 0; i < count; i++) {
                        struct quirc_code qc;
                        struct quirc_data qd;
                        quirc_extract(q, i, &qc);
                        quirc_decode_error_t err = quirc_decode(&qc, &qd);
                        if (err == QUIRC_SUCCESS) {
                            std::string payload(reinterpret_cast<const char*>(qd.payload),
                                                qd.payload_len);
                            Logger::Info("DMPair: decoded @y=" +
                                         std::to_string(y_scale) + " ch=" +
                                         gray_names[gm] + ": \"" + payload + "\"");
                            if (payload.size() == 6) {
                                bool all_digits = true;
                                for (char c : payload) {
                                    if (c < '0' || c > '9') { all_digits = false; break; }
                                }
                                if (all_digits) { found_code = payload; break; }
                            }
                        }
                    }
                    if (!found_code.empty()) break;
                }
                if (!found_code.empty()) break;
            }
            if (!found_code.empty()) {
                std::lock_guard<std::mutex> lock(scan_result_mutex_);
                scanned_code_ = found_code;
                Logger::Info("DMPair: scanned QR code: " + found_code);
                scan_running_ = false;
                quirc_destroy(q);
                return;
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
                Logger::Debug("DMPair: poll status=" + status + " peer=" + peer);
                if (status == "confirmed") {
                    // Both sides auto-confirmed on join
                    peer_name_ = peer;
                    skip_clock = false;
                    client.AddSession(session_id_, "", peer);
                    pda_.SaveDMSessions();
                    mode_ = Mode::COMPLETE;
                    RequestRender();
                    return;
                } else if (status == "joined") {
                    peer_name_ = peer;
                    skip_clock = false;
                    mode_ = Mode::JOINED;
                    RequestRender();
                    return;
                }
            }
        }

        // After bitmap refresh completes, restore speed and switch to text for overlay
        if (qr_needs_text_overlay_ && !display_.IsBuffered()) {
            display_.SetWriteDelay(saved_write_delay_);
            display_.SetTextMode();
            WriteCodeOverlay();
            qr_needs_text_overlay_ = false;
        }

        // Re-send full QR data + code overlay when display is idle.
        // Writes ALL modules (dark + light) plus 1px margin at NORM speed
        // so the entire QR self-heals even if the macro stamp was corrupted.
        if (!qr_needs_text_overlay_ && !display_.IsBuffered() &&
            now - last_qr_refresh_ >= QR_REFRESH_INTERVAL) {
            last_qr_refresh_ = now;

            display_.SetBitmapMode();
            saved_write_delay_ = display_.GetWriteDelay();
            // Use normal write speed for refresh (initial render uses SLOW)
            display_.BeginBuffered();
            QRGen qr;
            if (qr.Encode(code_)) {
                auto& matrix = qr.GetMatrix();
                constexpr int OFF = QRGen::OFFSET;
                constexpr int SZ = QRGen::SIZE;

                // QR modules only — quiet zone comes from the template stamp.
                // All QR modules: dark=0, light=255
                for (int r = 0; r < SZ; r++) {
                    for (int c = 0; c < SZ; c++) {
                        display_.WriteChar(c + OFF, r + OFF,
                                           matrix[r][c] ? 0 : 255);
                    }
                }
            }

            // After these writes flush, we'll restore speed, switch to text and write the code
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
            scan_beep_.Stop();
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
                    if (AudioEnabled() && error_sound_.IsLoaded()) error_sound_.Play();
                }
            } else {
                mode_ = Mode::FAILED;
                error_ = (pda_.GetDMClient().GetLastHttpCode() == 429)
                    ? "Rate limited"
                    : "Invalid or expired code";
                if (AudioEnabled() && error_sound_.IsLoaded()) error_sound_.Play();
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
            scan_beep_.Stop();
            StopScanning();
        }
        // Before leaving: check if a session was confirmed while we weren't polling
        if (!session_id_.empty()) {
            std::string status, peer;
            if (client.PairStatus(session_id_, status, peer)) {
                Logger::Info("DMPair: back-check status=" + status + " peer=" + peer);
                if (status == "confirmed" || status == "joined") {
                    client.AddSession(session_id_, "", peer);
                    pda_.SaveDMSessions();
                    Logger::Info("DMPair: recovered confirmed session on back");
                }
            }
        }
        // Pop screen — tell controller we're done handling back
        skip_clock = false;
        handle_back = false;
        return false;  // controller will pop
    }

    if (mode_ == Mode::CHOOSE) {
        // DIAL touch — contact 12 (col 1, row 2, near display row 4)
        if (key == "12") {
            // Reuse existing code if still valid
            if (!code_.empty() && MonotonicNow() < code_expires_) {
                last_poll_ = MonotonicNow();
                mode_ = Mode::RENDERING_QR;
                RequestRender();
                return true;
            }

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
                error_ = (pda_.GetDMClient().GetLastHttpCode() == 429)
                    ? "Rate limited"
                    : "Network error";
                if (AudioEnabled() && error_sound_.IsLoaded()) error_sound_.Play();
            }
            RequestRender();
            return true;
        }
        // SCAN touch — contact 52 (col 5, row 2, right side)
        if (key == "52") {
            mode_ = Mode::SCANNING;
            StartScanning();
            if (AudioEnabled() && scan_beep_.IsLoaded()) scan_beep_.Play(true);
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
                if (AudioEnabled() && error_sound_.IsLoaded()) error_sound_.Play();
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
