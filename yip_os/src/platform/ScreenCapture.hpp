#pragma once

#include <vector>
#include <cstdint>
#include <memory>

namespace YipOS {

// Captured screenshot as grayscale pixel buffer
struct Screenshot {
    std::vector<uint8_t> pixels;  // grayscale, row-major
    int width = 0;
    int height = 0;
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
