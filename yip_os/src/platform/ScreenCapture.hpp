#pragma once

#include <vector>
#include <cstdint>
#include <memory>

namespace YipOS {

// Captured screenshot as RGB pixel buffer
struct Screenshot {
    std::vector<uint8_t> pixels_r;  // red channel, row-major
    std::vector<uint8_t> pixels_g;  // green channel, row-major
    std::vector<uint8_t> pixels_b;  // blue channel, row-major
    int width = 0;
    int height = 0;

    // Compute grayscale using weighted luminance: (2R + 3G + B) / 6
    void ToGrayscale(std::vector<uint8_t>& out) const {
        out.resize(pixels_r.size());
        for (size_t i = 0; i < pixels_r.size(); i++) {
            out[i] = static_cast<uint8_t>(
                (pixels_r[i] * 2 + pixels_g[i] * 3 + pixels_b[i]) / 6);
        }
    }
};

// Abstract screen capture interface.
// Platform implementations provide the actual capture logic.
class ScreenCapture {
public:
    virtual ~ScreenCapture() = default;

    // Capture the primary display as a grayscale image.
    // Returns true on success.
    virtual bool Capture(Screenshot& out) = 0;

    static std::unique_ptr<ScreenCapture> Create();
};

} // namespace YipOS
