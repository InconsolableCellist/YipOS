#include "IMGScreen.hpp"
#include "app/PDAController.hpp"
#include "app/PDADisplay.hpp"
#include "core/Logger.hpp"
#include "core/Glyphs.hpp"
#include <algorithm>
#include <filesystem>
#include <cstdio>

namespace YipOS {

using namespace Glyphs;
namespace fs = std::filesystem;

// Bitmap grid constants (must match animator setup: 32x32)
static constexpr int BMP_COLS = VQEncoder::IMG_COLS; // 32
static constexpr int BMP_ROWS = VQEncoder::IMG_ROWS; // 32

IMGScreen::IMGScreen(PDAController& pda) : Screen(pda) {
    name = "IMG";
    macro_index = 27; // IMG list macro slot
    update_interval = 1.0f; // check for drops every second

    // Use the resolved assets path from main.cpp
    std::string assets = pda_.GetAssetsPath();
    if (assets.empty()) assets = "assets";

    // Images directory
    fs::path img_dir = fs::path(assets) / "images";
    if (fs::exists(img_dir) && fs::is_directory(img_dir)) {
        images_dir_ = img_dir.string();
    } else {
        // Fallback: images directly in assets
        images_dir_ = assets;
    }

    // Load VQ codebook from assets
    fs::path cb = fs::path(assets) / "vq_codebook.npy";
    if (!fs::exists(cb)) {
        // Try repo root (for development)
        for (const auto& p : {"vq_codebook.npy", "../vq_codebook.npy",
                              "../../vq_codebook.npy"}) {
            if (fs::exists(p)) { cb = p; break; }
        }
    }
    if (!encoder_.LoadCodebook(cb.string())) {
        Logger::Warning("IMG: VQ codebook not found at " + cb.string());
    }

    ScanImages();
}

void IMGScreen::ScanImages() {
    image_files_.clear();
    if (!fs::exists(images_dir_) || !fs::is_directory(images_dir_)) {
        Logger::Warning("IMG: images directory not found: " + images_dir_);
        return;
    }

    for (const auto& entry : fs::directory_iterator(images_dir_)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
            image_files_.push_back(entry.path().filename().string());
        }
    }
    std::sort(image_files_.begin(), image_files_.end());
    Logger::Info("IMG: found " + std::to_string(image_files_.size()) +
                 " images in " + images_dir_);
}

int IMGScreen::PageCount() const {
    int n = static_cast<int>(image_files_.size());
    if (n == 0) return 1;
    return (n + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
}

int IMGScreen::ItemCountOnPage() const {
    if (image_files_.empty()) return 0;
    int base = page_ * ROWS_PER_PAGE;
    int remaining = static_cast<int>(image_files_.size()) - base;
    return std::min(remaining, ROWS_PER_PAGE);
}

void IMGScreen::Render() {
    if (mode_ == DISPLAY) {
        WriteBitmap();
        return;
    }
    RenderFrame("IMG");
    if (!image_files_.empty()) {
        display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    }
    RenderRows();
    RenderPageIndicators();
    RenderStatusBar();
}

void IMGScreen::RenderDynamic() {
    if (mode_ == DISPLAY) {
        WriteBitmap();
        return;
    }
    if (!image_files_.empty()) {
        display_.WriteGlyph(COLS - 1, 1, G_RIGHT_A);
    }
    RenderRows();
    RenderPageIndicators();
    RenderClock();
    RenderCursor();
}

void IMGScreen::RenderRow(int i, bool selected) {
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(image_files_.size())) return;

    const auto& fname = image_files_[idx];
    int content_width = COLS - 2;
    std::string display_name = fname;
    // Strip extension for cleaner display
    auto dot = display_name.rfind('.');
    if (dot != std::string::npos) display_name = display_name.substr(0, dot);
    if (static_cast<int>(display_name.size()) > content_width) {
        display_name = display_name.substr(0, content_width);
    }

    // First 3 chars inverted = selection indicator, rest always plain
    static constexpr int SEL_WIDTH = 3;
    for (int c = 0; c < static_cast<int>(display_name.size()); c++) {
        int ch = static_cast<int>(display_name[c]);
        if (ch < 32 || ch > 126) ch = 32;
        if (selected && c < SEL_WIDTH) ch += INVERT_OFFSET;
        display_.WriteChar(1 + c, row, ch);
    }
}

void IMGScreen::RenderRows() {
    if (image_files_.empty()) {
        display_.WriteText(2, 3, "No images found");
        display_.WriteText(2, 4, images_dir_.substr(0, COLS - 4));
        return;
    }

    int items = ItemCountOnPage();
    for (int i = 0; i < items; i++) {
        RenderRow(i, i == cursor_);
    }
}

void IMGScreen::WriteSelectionMark(int i, bool selected) {
    int idx = page_ * ROWS_PER_PAGE + i;
    int row = 1 + i;
    if (idx >= static_cast<int>(image_files_.size())) return;

    std::string display_name = image_files_[idx];
    auto dot = display_name.rfind('.');
    if (dot != std::string::npos) display_name = display_name.substr(0, dot);

    for (int c = 0; c < 3 && c < static_cast<int>(display_name.size()); c++) {
        int ch = static_cast<int>(display_name[c]);
        if (ch < 32 || ch > 126) ch = 32;
        if (selected) ch += INVERT_OFFSET;
        display_.WriteChar(1 + c, row, ch);
    }
}

void IMGScreen::RefreshCursorRows(int old_cursor, int new_cursor) {
    display_.CancelBuffered();
    display_.BeginBuffered();
    if (old_cursor != new_cursor && old_cursor >= 0 && old_cursor < ItemCountOnPage()) {
        WriteSelectionMark(old_cursor, false);
    }
    if (new_cursor >= 0 && new_cursor < ItemCountOnPage()) {
        WriteSelectionMark(new_cursor, true);
    }
    RenderPageIndicators();
}

void IMGScreen::RenderPageIndicators() {
    if (!image_files_.empty()) {
        int global_idx = page_ * ROWS_PER_PAGE + cursor_ + 1;
        int total = static_cast<int>(image_files_.size());
        char pos[12];
        std::snprintf(pos, sizeof(pos), "%d/%d", global_idx, total);
        display_.WriteText(5, 7, pos);
    }

    if (PageCount() <= 1) return;
    if (page_ > 0) display_.WriteGlyph(0, 3, G_UP);
    if (page_ < PageCount() - 1) display_.WriteGlyph(0, 5, G_DOWN);
}

void IMGScreen::EnterDisplayMode(const std::string& image_path) {
    if (!encoder_.IsLoaded()) {
        Logger::Warning("IMG: codebook not loaded, cannot display");
        return;
    }

    Logger::Info("IMG: encoding " + image_path + "...");
    if (!encoder_.EncodeImage(image_path, vq_indices_)) {
        Logger::Error("IMG: failed to encode " + image_path);
        return;
    }

    current_image_ = image_path;
    mode_ = DISPLAY;
    skip_clock = true;
    handle_back = true;
    refresh_interval = -1.0f; // disable auto-refresh; we manage passes ourselves
    update_interval = 0.1f;   // fast polling for drops and pass management

    saved_write_delay_ = display_.GetWriteDelay();

    // Clear and switch to bitmap mode
    display_.CancelBuffered();
    display_.ClearScreen();
    display_.SetBitmapMode();

    // Start progressive passes: fast → medium → slow → done
    pass_ = 0;
    pass_done_ = false;
    StartPass();

    Logger::Info("IMG: displaying " + image_path);
}

void IMGScreen::StartPass() {
    float delay = (pass_ < PASS_COUNT) ? PASS_DELAYS[pass_] : REFRESH_DELAY;
    display_.SetWriteDelay(delay);
    display_.BeginBuffered();
    WriteBitmap();
    pass_active_ = true;

    int n = display_.BufferedRemaining();
    Logger::Info("IMG: pass " + std::to_string(pass_ + 1) +
                 "/" + std::to_string(PASS_COUNT) +
                 " (delay=" + std::to_string(static_cast<int>(delay * 1000)) +
                 "ms, " + std::to_string(n) + " writes)");
}

void IMGScreen::ExitDisplayMode() {
    mode_ = LIST;
    skip_clock = false;
    handle_back = false;
    refresh_interval = 0;
    update_interval = 1.0f; // back to slow polling in list mode
    pass_active_ = false;
    pass_done_ = false;

    // Restore write delay
    display_.SetWriteDelay(saved_write_delay_);

    // Re-render list
    pda_.StartRender(this);
    Logger::Info("IMG: back to list");
}

void IMGScreen::WriteBitmap() {
    // In bitmap mode, MoveCursor normalizes for 32x32 grid automatically
    for (int r = 0; r < BMP_ROWS; r++) {
        for (int c = 0; c < BMP_COLS; c++) {
            display_.WriteChar(c, r, vq_indices_[r][c]);
        }
    }
}

void IMGScreen::Update() {
    // Check for dropped image — clear any prior display and show the new one
    std::string drop = pda_.ConsumeDroppedImagePath();
    if (!drop.empty()) {
        Logger::Info("IMG: Update() consumed drop, cancelling current render (" +
                     std::to_string(display_.BufferedRemaining()) + " writes pending)");
        DropImage(drop);
        return; // don't process passes this tick, we just started fresh
    }

    // Progressive pass management: when buffer drains, start next pass
    if (mode_ == DISPLAY && pass_active_ && !display_.IsBuffered()) {
        pass_active_ = false;
        pass_++;
        if (pass_ < PASS_COUNT) {
            // More passes to go
            StartPass();
        } else if (!pass_done_) {
            // Final slow pass for good measure, then stop
            StartPass();
            pass_done_ = true;
        }
        // After pass_done_, do nothing — image stays on screen, no more redraws
    }
}

void IMGScreen::DropImage(const std::string& path) {
    Logger::Info("IMG: dropped image " + path);

    // Copy the image into the images directory if it's not already there
    fs::path src(path);
    fs::path dst = fs::path(images_dir_) / src.filename();
    if (src != dst && fs::exists(src)) {
        try {
            fs::create_directories(images_dir_);
            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            Logger::Info("IMG: copied " + src.filename().string() + " to images directory");
            ScanImages(); // refresh the file list
        } catch (const fs::filesystem_error& e) {
            Logger::Warning("IMG: failed to copy image: " + std::string(e.what()));
        }
    }

    EnterDisplayMode(path);
}

bool IMGScreen::OnInput(const std::string& key) {
    if (mode_ == DISPLAY) {
        // Any input in display mode goes back to list
        if (key == "TL" || key == "Joystick" || key == "TR") {
            ExitDisplayMode();
            return true;
        }
        return true; // absorb all input in display mode
    }

    // LIST mode
    if (image_files_.empty()) return false;

    if (key == "Joystick") {
        int items = ItemCountOnPage();
        if (items == 0) return true;
        int old_cursor = cursor_;
        cursor_ = (cursor_ + 1) % items;
        RefreshCursorRows(old_cursor, cursor_);
        return true;
    }

    if (key == "TR") {
        int idx = page_ * ROWS_PER_PAGE + cursor_;
        if (idx < static_cast<int>(image_files_.size())) {
            std::string full_path = (fs::path(images_dir_) / image_files_[idx]).string();
            EnterDisplayMode(full_path);
        }
        return true;
    }

    if (key == "ML" && PageCount() > 1 && page_ > 0) {
        page_--;
        cursor_ = 0;
        pda_.StartRender(this);
        return true;
    }
    if (key == "BL" && PageCount() > 1 && page_ < PageCount() - 1) {
        page_++;
        cursor_ = 0;
        pda_.StartRender(this);
        return true;
    }

    return false;
}

} // namespace YipOS
