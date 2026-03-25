#ifdef __linux__

#include "ScreenCapture.hpp"
#include "core/Logger.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace YipOS {

class ScreenCaptureLinux : public ScreenCapture {
public:
    ~ScreenCaptureLinux() override {
        if (display_) XCloseDisplay(display_);
    }

    bool Capture(Screenshot& out) override {
        if (!display_) {
            display_ = XOpenDisplay(nullptr);
            if (!display_) {
                Logger::Warning("ScreenCapture: cannot open X display");
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
                // Fast luminance approximation: (r + r + g + g + g + b) / 6
                out.pixels[y * w + x] = static_cast<uint8_t>((r + r + g + g + g + b) / 6);
            }
        }

        XDestroyImage(img);
        return true;
    }

private:
    Display* display_ = nullptr;
};

std::unique_ptr<ScreenCapture> ScreenCapture::Create() {
    return std::make_unique<ScreenCaptureLinux>();
}

} // namespace YipOS

#endif // __linux__
