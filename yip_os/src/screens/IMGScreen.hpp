#pragma once

#include "Screen.hpp"
#include "img/VQEncoder.hpp"
#include <string>
#include <vector>

namespace YipOS {

class IMGScreen : public Screen {
public:
    IMGScreen(PDAController& pda);

    void Render() override;
    void RenderDynamic() override;
    void Update() override;
    bool OnInput(const std::string& key) override;

    // External drop: set a path to immediately encode and display
    void DropImage(const std::string& path);

private:
    enum SubMode { LIST, DISPLAY };

    void ScanImages();
    void RenderRows();
    void RenderRow(int i, bool selected);
    void RefreshCursorRows(int old_cursor, int new_cursor);
    void WriteSelectionMark(int i, bool selected);
    void RenderPageIndicators();
    void EnterDisplayMode(const std::string& image_path);
    void ExitDisplayMode();
    void StartPass();
    void WriteBitmap();

    SubMode mode_ = LIST;
    std::string images_dir_;

    // List state
    std::vector<std::string> image_files_; // filenames only
    int cursor_ = 0;
    int page_ = 0;
    static constexpr int ROWS_PER_PAGE = 6;
    int PageCount() const;
    int ItemCountOnPage() const;

    // Display state
    VQEncoder encoder_;
    VQEncoder::IndexGrid vq_indices_{};
    std::string current_image_;
    float saved_write_delay_ = 0.07f;

    // Progressive pass system: fast → medium → slow → continuous refresh
    int pass_ = 0;
    bool pass_active_ = false; // true while a pass is being flushed
    bool pass_done_ = false;   // true after all passes complete (no more redraws)
    static constexpr int PASS_COUNT = 3;
    static constexpr float PASS_DELAYS[PASS_COUNT] = {0.01f, 0.04f, 0.07f};
    static constexpr float REFRESH_DELAY = 0.07f; // continuous refresh speed

    // Drop support
    std::string pending_drop_;
};

} // namespace YipOS
