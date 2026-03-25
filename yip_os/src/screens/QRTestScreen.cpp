#include "QRTestScreen.hpp"
#include "QRTestData.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"

namespace YipOS {

QRTestScreen::QRTestScreen(PDAController& pda) : Screen(pda) {
    name = "QRTEST";
    macro_index = -1;  // we handle rendering ourselves
    skip_clock = true;
    handle_back = true;
    update_interval = 0.1f;
}

void QRTestScreen::Render() {
    StartQRRender();
}

void QRTestScreen::RenderDynamic() {
    // Nothing to update dynamically — the buffered writes handle it
}

void QRTestScreen::Update() {
    // Check if buffered render is complete
    if (rendering_ && !display_.IsBuffered()) {
        rendering_ = false;
        display_.SetWriteDelay(saved_write_delay_);
        Logger::Info("QRTEST: render complete");
    }
}

bool QRTestScreen::OnInput(const std::string& key) {
    if (key == "TL" || key == "Joystick" || key == "ML") {
        // Exit: restore text mode and pop
        display_.CancelBuffered();
        display_.SetWriteDelay(saved_write_delay_);
        display_.SetTextMode();
        rendering_ = false;
        handle_back = false;  // let controller handle the pop
        return false;         // not consumed — controller will pop
    }
    return true;  // consume all other input while rendering
}

void QRTestScreen::StartQRRender() {
    saved_write_delay_ = display_.GetWriteDelay();

    // Step 1: Clear screen
    display_.ClearScreen();

    // Step 2: Stamp QR template macro (finder patterns, timing, quiet zone)
    display_.SetMacroMode();
    display_.StampMacro(QRTest::QR_TEMPLATE_MACRO);

    // Step 3: Switch to bitmap mode and write data modules
    display_.SetBitmapMode();
    display_.SetWriteDelay(0.07f);  // SLOW mode for reliability
    display_.BeginBuffered();

    for (int i = 0; i < QRTest::LIGHT_MODULE_COUNT; i++) {
        const auto& mod = QRTest::LIGHT_MODULES[i];
        display_.WriteChar(mod.col, mod.row, QRTest::VQ_WHITE);
    }

    rendering_ = true;
    Logger::Info("QRTEST: started render (" +
                 std::to_string(QRTest::LIGHT_MODULE_COUNT) +
                 " data writes queued)");
}

} // namespace YipOS
