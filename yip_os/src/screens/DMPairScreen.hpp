#pragma once

#include "Screen.hpp"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>

namespace YipOS {

class ScreenCapture;

class DMPairScreen : public Screen {
public:
    DMPairScreen(PDAController& pda);
    ~DMPairScreen() override;

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

private:
    void RenderContent();
    void StartQRRender();
    void StartScanning();
    void StopScanning();

    enum class Mode {
        CHOOSE,         // choose create (QR) or scan (capture)
        CREATING,       // creating session on worker...
        RENDERING_QR,   // stamping QR on CRT, waiting for peer
        SHOW_CODE,      // QR rendered, also showing text code, polling
        SCANNING,       // capturing screen + looking for QR
        JOINED,         // peer joined, confirm?
        JOINING,        // joining via code entered in ImGui
        COMPLETE,       // pairing complete
        FAILED
    };

    Mode mode_ = Mode::CHOOSE;
    std::string code_;
    std::string session_id_;
    std::string peer_name_;
    std::string error_;
    double code_expires_ = 0;
    double last_poll_ = 0;
    float saved_write_delay_ = 0;
    bool qr_rendering_ = false;

    // Scanner thread state
    std::unique_ptr<ScreenCapture> screen_capture_;
    std::unique_ptr<std::thread> scan_thread_;
    std::atomic<bool> scan_running_{false};
    std::mutex scan_result_mutex_;
    std::string scanned_code_;  // set by scan thread when QR found

    static constexpr double POLL_INTERVAL = 3.0;
    static constexpr double CODE_TTL = 300.0;
    static constexpr double SCAN_INTERVAL = 2.0;  // seconds between captures
    static constexpr int QR_TEMPLATE_MACRO = 37;
};

} // namespace YipOS
