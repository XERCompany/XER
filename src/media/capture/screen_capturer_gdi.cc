#include "media/capture/screen_capturer_gdi.h"

#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif

namespace XER::Media {

bool ScreenCapturerGdi::Start(const std::string& target_id, Callback cb,
                              std::string* error_out) {
    if (running_.exchange(true)) return false;
    cb_ = std::move(cb);
    (void)target_id;
    const auto period = std::chrono::milliseconds(1000 / fps_);
    thread_ = std::thread([this, period] {
        while (running_.load()) {
            CapturedVideoFrame frame;
            std::string err;
            if (CaptureOnce(&frame, &err) && cb_) cb_(std::move(frame));
            std::this_thread::sleep_for(period);
        }
    });
    if (error_out) error_out->clear();
    return true;
}

void ScreenCapturerGdi::Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

std::vector<std::string> ScreenCapturerGdi::EnumerateTargets() {
    return {"primary"};
}

bool ScreenCapturerGdi::CaptureOnce(CapturedVideoFrame* out,
                                    std::string* error_out) {
#ifdef _WIN32
    if (!out) return false;
    HDC screen = GetDC(nullptr);
    if (!screen) {
        if (error_out) *error_out = "GetDC failed";
        return false;
    }
    const int w = GetSystemMetrics(SM_CXSCREEN);
    const int h = GetSystemMetrics(SM_CYSCREEN);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof bi;
    bi.biWidth = w;
    bi.biHeight = -h;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    out->pixels.resize(static_cast<size_t>(w) * h * 4);
    GetDIBits(mem, bmp, 0, h, out->pixels.data(),
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    out->width = w;
    out->height = h;
    out->stride = w * 4;
    out->timestamp = 0;

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return true;
#else
    if (error_out) *error_out = "GDI capture is Windows-only";
    return false;
#endif
}

}  // namespace XER::Media
