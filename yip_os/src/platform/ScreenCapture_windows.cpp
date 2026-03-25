#ifdef _WIN32

#include "ScreenCapture.hpp"
#include "core/Logger.hpp"

// DXGI Desktop Duplication API
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace YipOS {

class ScreenCaptureWindows : public ScreenCapture {
public:
    ~ScreenCaptureWindows() override = default;

    bool Capture(Screenshot& out) override {
        if (!initialized_ && !Init()) return false;

        DXGI_OUTDUPL_FRAME_INFO frame_info{};
        ComPtr<IDXGIResource> resource;

        // Release previous frame if held
        if (frame_held_) {
            dup_->ReleaseFrame();
            frame_held_ = false;
        }

        HRESULT hr = dup_->AcquireNextFrame(500, &frame_info, &resource);
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                Logger::Debug("ScreenCapture: timeout, no new frame");
                return false;
            }
            Logger::Warning("ScreenCapture: AcquireNextFrame failed");
            initialized_ = false;
            return false;
        }
        frame_held_ = true;

        ComPtr<ID3D11Texture2D> tex;
        hr = resource.As(&tex);
        if (FAILED(hr)) return false;

        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);

        // Create staging texture for CPU read
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        hr = device_->CreateTexture2D(&desc, nullptr, &staging);
        if (FAILED(hr)) return false;

        context_->CopyResource(staging.Get(), tex.Get());

        D3D11_MAPPED_SUBRESOURCE mapped;
        hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return false;

        int w = static_cast<int>(desc.Width);
        int h = static_cast<int>(desc.Height);
        out.width = w;
        out.height = h;
        out.pixels.resize(w * h);

        uint8_t* src = static_cast<uint8_t*>(mapped.pData);
        for (int y = 0; y < h; y++) {
            uint8_t* row = src + y * mapped.RowPitch;
            for (int x = 0; x < w; x++) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r = row[x * 4 + 2];
                out.pixels[y * w + x] = static_cast<uint8_t>((r + r + g + g + g + b) / 6);
            }
        }

        context_->Unmap(staging.Get(), 0);
        return true;
    }

private:
    bool Init() {
        D3D_FEATURE_LEVEL feature_level;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &device_, &feature_level, &context_);
        if (FAILED(hr)) {
            Logger::Warning("ScreenCapture: D3D11CreateDevice failed");
            return false;
        }

        ComPtr<IDXGIDevice> dxgi_device;
        device_.As(&dxgi_device);
        ComPtr<IDXGIAdapter> adapter;
        dxgi_device->GetAdapter(&adapter);
        ComPtr<IDXGIOutput> output;
        adapter->EnumOutputs(0, &output);
        ComPtr<IDXGIOutput1> output1;
        output.As(&output1);

        hr = output1->DuplicateOutput(device_.Get(), &dup_);
        if (FAILED(hr)) {
            Logger::Warning("ScreenCapture: DuplicateOutput failed");
            return false;
        }

        initialized_ = true;
        return true;
    }

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> dup_;
    bool initialized_ = false;
    bool frame_held_ = false;
};

std::unique_ptr<ScreenCapture> ScreenCapture::Create() {
    return std::make_unique<ScreenCaptureWindows>();
}

} // namespace YipOS

#endif // _WIN32
