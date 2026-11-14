#include "photo_browser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <system_error>

#include "image/io/image_loader.h"
#include "image/io/image_saver.h"
#include "image/tools/crop_tool.h"
#include "helper/path_utils.h"

namespace preview {
namespace fs = std::filesystem;
namespace Path = XER::Helper::Path;

namespace {

constexpr std::string_view kExtensions[] = {
    ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp",
    ".tif", ".tiff", ".heic", ".heif", ".avif", ".jxl",
    ".svg", ".psd", ".exr", ".hdr",
};

std::string LowerExt(const fs::path& p) {
    std::string ext = Path::ToUtf8(p.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

std::string FileKey(const fs::path& p) {
    std::string s = Path::ToUtf8(p.filename());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool HasExtension(const fs::path& p) {
    const std::string ext = LowerExt(p);
    for (const auto& e : kExtensions) {
        if (ext == e) return true;
    }
    return false;
}

bool IsImageFile(const fs::directory_entry& e) {
    return e.is_regular_file() && HasExtension(e.path());
}

}  // namespace

bool IsSupportedImageExtension(const std::string& path) {
    return HasExtension(Path::FromUtf8(path));
}

bool NormalizeToRGBA8(const image::ImageBuffer& src, image::ImageBuffer& dst) {
    const int w = src.Width();
    const int h = src.Height();
    if (w <= 0 || h <= 0) return false;

    dst.Allocate(w, h, image::PixelFormat::RGBA8, image::ColorSpace::sRGB);
    const std::size_t px = static_cast<std::size_t>(w) * h;
    const uint8_t* s = src.Data();
    uint8_t* d = dst.Data();
    if (s == nullptr || d == nullptr) return false;

    switch (src.Format()) {
        case image::PixelFormat::RGBA8:
            std::copy(s, s + px * 4, d);
            return true;
        case image::PixelFormat::BGRA8:
            for (std::size_t i = 0; i < px; ++i) {
                d[i * 4 + 0] = s[i * 4 + 2];
                d[i * 4 + 1] = s[i * 4 + 1];
                d[i * 4 + 2] = s[i * 4 + 0];
                d[i * 4 + 3] = s[i * 4 + 3];
            }
            return true;
        case image::PixelFormat::RGB8:
            for (std::size_t i = 0; i < px; ++i) {
                d[i * 4 + 0] = s[i * 3 + 0];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 2];
                d[i * 4 + 3] = 255;
            }
            return true;
        case image::PixelFormat::BGR8:
            for (std::size_t i = 0; i < px; ++i) {
                d[i * 4 + 0] = s[i * 3 + 2];
                d[i * 4 + 1] = s[i * 3 + 1];
                d[i * 4 + 2] = s[i * 3 + 0];
                d[i * 4 + 3] = 255;
            }
            return true;
        case image::PixelFormat::Gray8:
            for (std::size_t i = 0; i < px; ++i) {
                const uint8_t g = s[i];
                d[i * 4 + 0] = g;
                d[i * 4 + 1] = g;
                d[i * 4 + 2] = g;
                d[i * 4 + 3] = 255;
            }
            return true;
        default:
            return false;
    }
}

const std::string& PhotoBrowser::CurrentPath() const {
    static const std::string kEmpty;
    return HasCurrent() ? files_[current_] : kEmpty;
}

bool PhotoBrowser::Open(const std::string& path) {
    files_.clear();
    current_ = 0;
    base_ = image::ImageBuffer{};
    display_ = image::ImageBuffer{};
    brightness_ = 0.0f;
    contrast_ = 100.0f;
    dirty_ = false;
    revision_ = 0;
    status_message_.clear();

    std::error_code ec;
    const fs::path input = fs::weakly_canonical(Path::FromUtf8(path), ec);
    if (ec || !fs::is_regular_file(input)) {
        status_message_ = "Cannot open: " + path;
        return false;
    }
    if (!IsSupportedImageExtension(Path::ToUtf8(input))) {
        status_message_ = "Unsupported image format: " + Path::ToUtf8(input.extension());
        return false;
    }

    const fs::path dir = input.parent_path();
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (IsImageFile(entry)) {
            files_.push_back(Path::ToUtf8(entry.path().lexically_normal()));
        }
    }
    if (files_.empty()) {
        status_message_ = "No images found in folder";
        return false;
    }
    std::sort(files_.begin(), files_.end(),
              [](const std::string& a, const std::string& b) {
                  return FileKey(Path::FromUtf8(a)) < FileKey(Path::FromUtf8(b));
              });

    const std::string wanted = FileKey(input);
    const auto it = std::find_if(files_.begin(), files_.end(),
                                 [&](const std::string& f) {
                                     return FileKey(Path::FromUtf8(f)) == wanted;
                                 });
    current_ = (it != files_.end())
                   ? static_cast<std::size_t>(it - files_.begin())
                   : 0;
    return LoadCurrent();
}

bool PhotoBrowser::GoTo(int delta) {
    if (files_.empty()) return false;
    const long long next = static_cast<long long>(current_) + delta;
    const long long clamped = std::clamp<long long>(next, 0,
                                                    static_cast<long long>(files_.size() - 1));
    if (clamped == static_cast<long long>(current_)) return false;
    current_ = static_cast<std::size_t>(clamped);
    return LoadCurrent();
}

bool PhotoBrowser::GoToIndex(std::size_t index) {
    if (index >= files_.size() || index == current_) return false;
    current_ = index;
    return LoadCurrent();
}

bool PhotoBrowser::LoadCurrent() {
    if (!HasCurrent()) return false;

    base_ = image::ImageBuffer{};
    display_ = image::ImageBuffer{};
    dirty_ = false;
    brightness_ = 0.0f;
    contrast_ = 100.0f;

    image::ImageLoader loader;
    image::ImageDescriptor desc;
    auto raw = loader.Load(files_[current_], desc);
    if (!raw || !raw->IsValid()) {
        status_message_ = "Failed to decode: " + files_[current_];
        return false;
    }
    if (!NormalizeToRGBA8(*raw, base_)) {
        status_message_ = "Unsupported pixel format (" +
                          std::to_string(static_cast<int>(raw->Format())) + ")";
        return false;
    }
    RebuildDisplay();
    ++revision_;
    status_message_.clear();
    return true;
}

bool PhotoBrowser::RequireLoaded(const char* op) const {
    if (!base_.IsValid()) {
        // const method cannot set status; callers check IsLoaded first.
        (void)op;
        return false;
    }
    return true;
}

void PhotoBrowser::MarkEdited() {
    dirty_ = true;
    ++revision_;
}

void PhotoBrowser::RebuildDisplay() {
    if (!base_.IsValid()) return;
    display_ = base_.Clone();

    const bool needs_color =
        std::abs(brightness_) > 0.001f || std::abs(contrast_ - 100.0f) > 0.001f;
    if (!needs_color) return;

    std::vector<uint8_t> lut(256);
    const float contrast_f = contrast_ / 100.0f;
    const float brightness_f = brightness_ * 2.55f;  // ±100 -> ±255
    for (int i = 0; i < 256; ++i) {
        const float v = (i - 128.0f) * contrast_f + 128.0f + brightness_f;
        lut[static_cast<std::size_t>(i)] = static_cast<uint8_t>(
            std::clamp(v, 0.0f, 255.0f));
    }
    display_.ApplyLUT(lut, lut, lut);
}

bool PhotoBrowser::RotateCW() {
    if (!RequireLoaded("rotate")) return false;
    image::ImageBuffer next = image::CropTool::Rotate90(base_, 1);
    if (!next.IsValid()) return false;
    base_ = std::move(next);
    RebuildDisplay();
    MarkEdited();
    return true;
}

bool PhotoBrowser::RotateCCW() {
    if (!RequireLoaded("rotate")) return false;
    image::ImageBuffer next = image::CropTool::Rotate90(base_, 3);
    if (!next.IsValid()) return false;
    base_ = std::move(next);
    RebuildDisplay();
    MarkEdited();
    return true;
}

bool PhotoBrowser::FlipHorizontal() {
    if (!RequireLoaded("flip")) return false;
    image::ImageBuffer next = image::CropTool::FlipH(base_);
    if (!next.IsValid()) return false;
    base_ = std::move(next);
    RebuildDisplay();
    MarkEdited();
    return true;
}

bool PhotoBrowser::FlipVertical() {
    if (!RequireLoaded("flip")) return false;
    image::ImageBuffer next = image::CropTool::FlipV(base_);
    if (!next.IsValid()) return false;
    base_ = std::move(next);
    RebuildDisplay();
    MarkEdited();
    return true;
}

bool PhotoBrowser::Crop(int x, int y, int w, int h) {
    if (!RequireLoaded("crop")) return false;
    const int iw = base_.Width();
    const int ih = base_.Height();
    x = std::clamp(x, 0, iw - 1);
    y = std::clamp(y, 0, ih - 1);
    w = std::clamp(w, 1, iw - x);
    h = std::clamp(h, 1, ih - y);
    image::ImageBuffer next = image::CropTool::Crop(base_, x, y, w, h);
    if (!next.IsValid()) return false;
    base_ = std::move(next);
    RebuildDisplay();
    MarkEdited();
    return true;
}

void PhotoBrowser::SetBrightness(float value) {
    value = std::clamp(value, -100.0f, 100.0f);
    if (value == brightness_) return;
    brightness_ = value;
    RebuildDisplay();
    MarkEdited();
}

void PhotoBrowser::SetContrast(float value) {
    value = std::clamp(value, 0.0f, 200.0f);
    if (value == contrast_) return;
    contrast_ = value;
    RebuildDisplay();
    MarkEdited();
}

void PhotoBrowser::ResetColor() {
    if (std::abs(brightness_) < 0.001f && std::abs(contrast_ - 100.0f) < 0.001f) {
        return;
    }
    brightness_ = 0.0f;
    contrast_ = 100.0f;
    RebuildDisplay();
    MarkEdited();
}

bool PhotoBrowser::Revert() {
    if (files_.empty()) return false;
    dirty_ = false;
    return LoadCurrent();
}

bool PhotoBrowser::Save() {
    if (!IsLoaded() || !HasCurrent()) {
        status_message_ = "Nothing to save";
        return false;
    }
    image::ImageSaver saver;
    if (saver.Save(display_, files_[current_], 92)) {
        dirty_ = false;
        status_message_.clear();
        return true;
    }
    status_message_ = "Save failed for " + Path::ToUtf8(Path::FromUtf8(files_[current_]).extension()) +
                      " - use Export PNG";
    return false;
}

bool PhotoBrowser::ExportPng(std::string* out_path) {
    if (!IsLoaded() || !HasCurrent()) {
        status_message_ = "Nothing to export";
        return false;
    }
    const fs::path src = Path::FromUtf8(files_[current_]);
    fs::path dst = src.parent_path() / (Path::ToUtf8(src.stem()) + ".png");
    std::error_code ec;
    if (fs::equivalent(dst, src, ec)) {
        dst = src.parent_path() /
              (Path::ToUtf8(src.stem()) + "_xER.png");
    }
    const std::string dst_utf8 = Path::ToUtf8(dst);
    image::ImageSaver saver;
    if (!saver.SavePng(display_, dst_utf8)) {
        status_message_ = "PNG export failed: " + dst_utf8;
        return false;
    }
    if (out_path) *out_path = dst_utf8;
    status_message_ = "Exported " + Path::ToUtf8(dst.filename());
    return true;
}

}  // namespace preview
