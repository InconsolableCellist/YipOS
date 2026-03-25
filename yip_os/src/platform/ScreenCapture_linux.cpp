#ifdef __linux__

#include "ScreenCapture.hpp"
#include "core/Logger.hpp"
#include <cstdlib>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace YipOS {

class ScreenCaptureLinux : public ScreenCapture {
public:
    ScreenCaptureLinux() {
        // X11 root window capture segfaults on Wayland — don't even try
        if (std::getenv("WAYLAND_DISPLAY")) {
            Logger::Warning("ScreenCapture: Wayland detected, screen capture not supported");
            disabled_ = true;
        }
    }

    ~ScreenCaptureLinux() override {
        if (display_) XCloseDisplay(display_);
    }

    bool Capture(Screenshot& out) override {
        if (disabled_) return false;

        if (!display_) {
            display_ = XOpenDisplay(nullptr);
            if (!display_) {
                Logger::Warning("ScreenCapture: cannot open X display");
                disabled_ = true;
                return false;
            }
        }

        Window root = DefaultRootWindow(display_);
        XWindowAttributes attrs;
        XGetWindowAttributes(display_, root, &attrs);
        int w = attrs.width;
        int h = attrs.height;

        XImage* img = XGetImage(display_, root, 0, 0, w, h, AllPlanes, ZPixmap);
        if (!img) {
            Logger::Warning("ScreenCapture: XGetImage failed");
            disabled_ = true;
            XCloseDisplay(display_);
            display_ = nullptr;
            return false;
        }

        out.width = w;
        out.height = h;
        out.pixels.resize(w * h);

        // Convert to grayscale
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                unsigned long pixel = XGetPixel(img, x, y);
                uint8_t r = (pixel >> 16) & 0xFF;
                uint8_t g = (pixel >> 8) & 0xFF;
                uint8_t b = pixel & 0xFF;
                out.pixels[y * w + x] = static_cast<uint8_t>((r + r + g + g + g + b) / 6);
            }
        }

        XDestroyImage(img);
        return true;
    }

private:
    Display* display_ = nullptr;
    bool disabled_ = false;
};

std::unique_ptr<ScreenCapture> ScreenCapture::Create() {
    return std::make_unique<ScreenCaptureLinux>();
}

} // namespace YipOS

#endif // __linux__
