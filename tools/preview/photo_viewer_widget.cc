#include "photo_viewer_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "ui/core/window.h"
#include "ui/rendering/renderer.h"
#include "ui/theme/tokens.h"
#include "ui/widgets/button.h"
#include "ui/widgets/label.h"
#include "ui/widgets/slider.h"

namespace preview {

// Out-of-class definitions cannot see the private member aliases; import the
// layout types at namespace scope for signatures.
using XER::UI::Layout::Rect;
using XER::UI::Layout::Point;

namespace {

constexpr float kToolbarH = 42.0f;
constexpr float kEditbarH = 50.0f;
constexpr float kStripH = 118.0f;
constexpr float kStatusH = 34.0f;
constexpr float kPad = 10.0f;
constexpr float kGap = 6.0f;
constexpr float kBtnH = 30.0f;
constexpr float kBtnW = 82.0f;

// Filmstrip cell geometry.
constexpr float kCellW = 148.0f;
constexpr float kCellH = 106.0f;
constexpr float kThumbW = 128.0f;
constexpr float kThumbH = 86.0f;
constexpr float kCellPadX = (kCellW - kThumbW) * 0.5f;
constexpr float kCellPadY = (kCellH - kThumbH) * 0.5f;

XER::UI::Theme::Color Hex(uint32_t rgb, float a = 1.0f) {
    return XER::UI::Theme::Color{
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f,
        a};
}

std::string BaseName(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

}  // namespace

PhotoViewerWidget::PhotoViewerWidget(PhotoBrowser* browser,
                                     XER::UI::Core::Window* window)
    : browser_(browser), window_(window),
      thumbs_(std::make_unique<ThumbCache>()) {
    using XER::UI::Widgets::ButtonStyle;

    auto make_btn = [this](const char* text, ButtonStyle style = ButtonStyle::Secondary) {
        auto b = std::make_shared<Button>(text, style);
        AddChild(b);
        return b;
    };

    btn_prev_ = make_btn("< Prev");
    btn_next_ = make_btn("Next >");
    btn_rot_cw_ = make_btn("Rotate CW");
    btn_rot_ccw_ = make_btn("Rotate CCW");
    btn_flip_h_ = make_btn("Flip H");
    btn_flip_v_ = make_btn("Flip V");
    btn_crop_ = make_btn("Crop");
    btn_fit_ = make_btn("Fit");

    btn_apply_ = make_btn("Apply", ButtonStyle::Primary);
    btn_cancel_ = make_btn("Cancel", ButtonStyle::Ghost);
    btn_save_ = make_btn("Save", ButtonStyle::Primary);
    btn_png_ = make_btn("PNG", ButtonStyle::Secondary);
    btn_revert_ = make_btn("Revert", ButtonStyle::Ghost);

    label_bright_ = std::make_shared<Label>("Bright", true);
    label_contrast_ = std::make_shared<Label>("Contrast", true);
    AddChild(label_bright_);
    AddChild(label_contrast_);

    slider_bright_ = std::make_shared<Slider>(-100.0f, 100.0f, 0.0f);
    slider_contrast_ = std::make_shared<Slider>(0.0f, 200.0f, 100.0f);
    AddChild(slider_bright_);
    AddChild(slider_contrast_);

    btn_prev_->SetOnClick([this] { Navigate(-1); });
    btn_next_->SetOnClick([this] { Navigate(1); });
    btn_rot_cw_->SetOnClick([this] {
        browser_->RotateCW();
        ResetView();
        Invalidate();
    });
    btn_rot_ccw_->SetOnClick([this] {
        browser_->RotateCCW();
        ResetView();
        Invalidate();
    });
    btn_flip_h_->SetOnClick([this] {
        browser_->FlipHorizontal();
        ResetView();
        Invalidate();
    });
    btn_flip_v_->SetOnClick([this] {
        browser_->FlipVertical();
        ResetView();
        Invalidate();
    });
    btn_crop_->SetOnClick([this] {
        if (crop_mode_) ExitCropMode(); else EnterCropMode();
        Invalidate();
    });
    btn_fit_->SetOnClick([this] { ResetView(); Invalidate(); });
    btn_apply_->SetOnClick([this] { ApplyCrop(); Invalidate(); });
    btn_cancel_->SetOnClick([this] { ExitCropMode(); Invalidate(); });
    btn_save_->SetOnClick([this] {
        browser_->Save();
        UpdateStatusText();
        Invalidate();
    });
    btn_png_->SetOnClick([this] {
        browser_->ExportPng();
        UpdateStatusText();
        Invalidate();
    });
    btn_revert_->SetOnClick([this] {
        browser_->Revert();
        ResetView();
        UpdateStatusText();
        Invalidate();
    });

    slider_bright_->SetOnValueChanged([this](float v) {
        browser_->SetBrightness(v);
        Invalidate();
    });
    slider_contrast_->SetOnValueChanged([this](float v) {
        browser_->SetContrast(v);
        Invalidate();
    });

    SetFocusable(true);
    ExitCropMode();
    UpdateStatusText();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void PhotoViewerWidget::Layout(const Rect& bounds) {
    bounds_ = bounds;
    PositionChildren(bounds, CachedLayoutTokens());
}

void PhotoViewerWidget::Layout(const Rect& bounds,
                               const XER::UI::Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    Layout(bounds);
}

void PhotoViewerWidget::PositionChildren(const Rect& bounds,
                                         const XER::UI::Theme::Tokens& tokens) {
    const float x = bounds.x + kPad;
    const float y1 = bounds.y + 6.0f;

    // Row 1: navigation + geometric tools.
    const std::vector<std::shared_ptr<Button>> row1 = {
        btn_prev_, btn_next_, btn_rot_cw_, btn_rot_ccw_,
        btn_flip_h_, btn_flip_v_, btn_crop_, btn_fit_,
    };
    float bx = x;
    for (const auto& b : row1) {
        b->Layout(Rect{bx, y1, kBtnW, kBtnH}, tokens);
        bx += kBtnW + kGap;
    }

    // Row 2: crop actions, color sliders, file ops.
    const float y2 = bounds.y + kToolbarH + 4.0f;
    btn_apply_->Layout(Rect{x, y2, 76.0f, kBtnH}, tokens);
    btn_cancel_->Layout(Rect{x + 82.0f, y2, 76.0f, kBtnH}, tokens);

    label_bright_->Layout(Rect{x + 176.0f, y2 + 7.0f, 56.0f, 16.0f}, tokens);
    slider_bright_->Layout(Rect{x + 234.0f, y2 + 4.0f, 190.0f, kBtnH}, tokens);

    label_contrast_->Layout(Rect{x + 440.0f, y2 + 7.0f, 64.0f, 16.0f}, tokens);
    slider_contrast_->Layout(Rect{x + 506.0f, y2 + 4.0f, 190.0f, kBtnH}, tokens);

    const float fx = x + 716.0f;
    btn_save_->Layout(Rect{fx, y2, 72.0f, kBtnH}, tokens);
    btn_png_->Layout(Rect{fx + 78.0f, y2, 64.0f, kBtnH}, tokens);
    btn_revert_->Layout(Rect{fx + 148.0f, y2, 80.0f, kBtnH}, tokens);

    const float content_bottom = bounds.y + bounds.height - kStatusH - kStripH;
    viewport_ = Rect{bounds.x + kPad,
                     bounds.y + kToolbarH + kEditbarH,
                     bounds.width - 2 * kPad,
                     content_bottom - (bounds.y + kToolbarH + kEditbarH)};
    if (viewport_.width < 0) viewport_.width = 0;
    if (viewport_.height < 0) viewport_.height = 0;

    strip_ = Rect{bounds.x + kPad,
                  bounds.y + bounds.height - kStatusH - kStripH + 4.0f,
                  bounds.width - 2 * kPad,
                  kStripH - 8.0f};
    if (strip_.width < 0) strip_.width = 0;
    if (strip_.height < 0) strip_.height = 0;
}

XER::UI::Layout::Size PhotoViewerWidget::GetMinSize() const {
    return {860.0f, 620.0f};
}
XER::UI::Layout::Size PhotoViewerWidget::GetPreferredSize() const {
    return {1360.0f, 860.0f};
}
XER::UI::Layout::Size PhotoViewerWidget::GetMaxSize() const {
    return {4096.0f, 4096.0f};
}

// ---------------------------------------------------------------------------
// View transform
// ---------------------------------------------------------------------------

float PhotoViewerWidget::FitScale() const {
    if (!browser_->IsLoaded() || viewport_.width <= 0 || viewport_.height <= 0) {
        return 1.0f;
    }
    return std::min(viewport_.width / browser_->ImageWidth(),
                    viewport_.height / browser_->ImageHeight());
}

Rect PhotoViewerWidget::ImageDestRect() const {
    if (!browser_->IsLoaded()) return {};
    const float s = FitScale() * zoom_;
    const float w = browser_->ImageWidth() * s;
    const float h = browser_->ImageHeight() * s;
    return {pan_x_, pan_y_, w, h};
}

void PhotoViewerWidget::ResetView() {
    zoom_ = 1.0f;
    if (browser_->IsLoaded() && viewport_.width > 0 && viewport_.height > 0) {
        const Rect d = ImageDestRect();
        pan_x_ = viewport_.x + (viewport_.width - d.width) * 0.5f;
        pan_y_ = viewport_.y + (viewport_.height - d.height) * 0.5f;
    } else {
        pan_x_ = viewport_.x;
        pan_y_ = viewport_.y;
    }
}

void PhotoViewerWidget::ZoomAt(Point center, float factor) {
    const float old_zoom = zoom_;
    const float new_zoom = std::clamp(zoom_ * factor, 0.05f, 20.0f);
    if (new_zoom == old_zoom) return;

    // Image-space point under the cursor before the zoom change.
    const Rect before = ImageDestRect();
    float ix = 0.5f, iy = 0.5f;
    if (before.width > 1e-6f && before.height > 1e-6f &&
        browser_->IsLoaded()) {
        ix = (center.x - before.x) / before.width;
        iy = (center.y - before.y) / before.height;
        ix = std::clamp(ix, 0.0f, 1.0f);
        iy = std::clamp(iy, 0.0f, 1.0f);
    }
    zoom_ = new_zoom;
    const Rect after = ImageDestRect();
    pan_x_ = center.x - ix * after.width;
    pan_y_ = center.y - iy * after.height;
}

// ---------------------------------------------------------------------------
// Editing actions
// ---------------------------------------------------------------------------

void PhotoViewerWidget::Navigate(int delta) {
    if (browser_->GoTo(delta)) {
        ExitCropMode();
        ResetView();
    }
    UpdateStatusText();
    Invalidate();
}

void PhotoViewerWidget::EnterCropMode() {
    crop_mode_ = true;
    crop_drag_ = false;
    crop_rect_ = {};
    btn_apply_->SetEnabled(false);
    btn_cancel_->SetEnabled(true);
}

void PhotoViewerWidget::ExitCropMode() {
    crop_mode_ = false;
    crop_drag_ = false;
    crop_rect_ = {};
    btn_apply_->SetEnabled(false);
    btn_cancel_->SetEnabled(false);
}

void PhotoViewerWidget::ApplyCrop() {
    if (!crop_mode_ || crop_rect_.width < 4 || crop_rect_.height < 4) return;
    const Rect d = ImageDestRect();
    // Intersect selection with the image rect, convert to image pixels.
    const float ix0 = std::max(crop_rect_.x, d.x);
    const float iy0 = std::max(crop_rect_.y, d.y);
    const float ix1 = std::min(crop_rect_.x + crop_rect_.width, d.x + d.width);
    const float iy1 = std::min(crop_rect_.y + crop_rect_.height, d.y + d.height);
    if (ix1 - ix0 < 4 || iy1 - iy0 < 4) return;

    const float sx = browser_->ImageWidth() / d.width;
    const float sy = browser_->ImageHeight() / d.height;
    const int x = static_cast<int>((ix0 - d.x) * sx);
    const int y = static_cast<int>((iy0 - d.y) * sy);
    const int w = static_cast<int>((ix1 - ix0) * sx);
    const int h = static_cast<int>((iy1 - iy0) * sy);
    browser_->Crop(x, y, w, h);
    ExitCropMode();
    ResetView();
    UpdateStatusText();
}

// ---------------------------------------------------------------------------
// Filmstrip
// ---------------------------------------------------------------------------

Rect PhotoViewerWidget::StripCellRect(std::size_t index) const {
    return {strip_.x + 6.0f + static_cast<float>(index) * kCellW - strip_scroll_,
            strip_.y + 4.0f,
            kCellW - 8.0f,
            kCellH};
}

int PhotoViewerWidget::StripIndexAt(Point p) const {
    if (!strip_.Contains(p)) return -1;
    const float rel = p.x - strip_.x - 6.0f + strip_scroll_;
    const int idx = static_cast<int>(rel / kCellW);
    if (idx < 0 || idx >= static_cast<int>(browser_->Files().size())) return -1;
    const Rect cell = StripCellRect(static_cast<std::size_t>(idx));
    return cell.Contains(p) ? idx : -1;
}

void PhotoViewerWidget::ScrollStripToCurrent() {
    const std::size_t cur = browser_->CurrentIndex();
    const Rect cell = StripCellRect(cur);
    const float visible_left = strip_.x + 4.0f;
    const float visible_right = strip_.x + strip_.width - 4.0f;
    if (cell.x < visible_left) {
        strip_scroll_ -= (visible_left - cell.x);
    } else if (cell.x + cell.width > visible_right) {
        strip_scroll_ += (cell.x + cell.width) - visible_right;
    }
    const float max_scroll =
        6.0f + browser_->Files().size() * kCellW - (strip_.width - 8.0f);
    strip_scroll_ = std::clamp(strip_scroll_, 0.0f, std::max(0.0f, max_scroll));
}

void PhotoViewerWidget::EnsureStripRequests() {
    const auto& files = browser_->Files();
    if (files.empty() || strip_.width <= 0) return;
    const int first = std::max(0, static_cast<int>(strip_scroll_ / kCellW) - 1);
    const int last = std::min(static_cast<int>(files.size()) - 1,
                              static_cast<int>((strip_scroll_ + strip_.width) /
                                                   kCellW) + 1);
    for (int i = first; i <= last; ++i) {
        thumbs_->Request(files[static_cast<std::size_t>(i)]);
    }
}

void PhotoViewerWidget::RenderFilmstrip(XER::UI::Rendering::Renderer& renderer) {
    if (!browser_->IsOpen()) return;

    // Clip children so thumbs can't bleed outside the strip.
    renderer.PushClip(strip_);
    renderer.FillRect(strip_, Hex(0x15161C), 7.0f);
    renderer.StrokeRect(strip_, Hex(0x262932), 1.0f, 7.0f);

    const auto& files = browser_->Files();
    const int first = std::max(0, static_cast<int>(strip_scroll_ / kCellW) - 1);
    const int last = std::min(static_cast<int>(files.size()) - 1,
                              static_cast<int>((strip_scroll_ + strip_.width) /
                                                   kCellW) + 1);

    for (int i = first; i <= last; ++i) {
        const Rect cell = StripCellRect(static_cast<std::size_t>(i));
        if (cell.x + cell.width < strip_.x || cell.x > strip_.x + strip_.width) {
            continue;
        }
        const bool current =
            static_cast<std::size_t>(i) == browser_->CurrentIndex();
        const bool hovered = i == strip_hover_;

        // Cell background.
        renderer.FillRect(cell, Hex(current ? 0x232634 : (hovered ? 0x1E2029 : 0x1A1B22)),
                          6.0f);

        // Thumbnail image, letterboxed inside the thumb box.
        Thumbnail thumb;
        const std::string& path = files[static_cast<std::size_t>(i)];
        if (thumbs_->Get(path, thumb)) {
            const float box_x = cell.x + kCellPadX;
            const float box_y = cell.y + kCellPadY;
            const float scale = std::min(kThumbW / thumb.width,
                                         kThumbH / thumb.height);
            const float w = thumb.width * scale;
            const float h = thumb.height * scale;
            const Rect dst{box_x + (kThumbW - w) * 0.5f,
                           box_y + (kThumbH - h) * 0.5f, w, h};
            renderer.DrawFrameBgra(thumb.bgra.data(), thumb.width, thumb.height,
                                   dst, current ? 1.0f : 0.85f);
        } else {
            // Pending/failed placeholder.
            renderer.FillRect(
                Rect{cell.x + kCellPadX, cell.y + kCellPadY, kThumbW, kThumbH},
                Hex(0x111217), 4.0f);
        }

        // Border: accent for current, lighter for hover.
        const auto border = current ? Hex(0x4DA3FF, 1.0f)
                          : hovered ? Hex(0x3A3D4A, 1.0f)
                          : Hex(0x262932, 1.0f);
        renderer.StrokeRect(cell, border, current ? 2.0f : 1.0f, 6.0f);
    }
    renderer.PopClip();
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void PhotoViewerWidget::RebuildUpload() {
    if (!browser_->IsLoaded()) {
        bgra_.clear();
        uploaded_rev_ = ~std::uint64_t{0};
        uploaded_path_.clear();
        return;
    }
    if (uploaded_rev_ == browser_->Revision() &&
        uploaded_path_ == browser_->CurrentPath()) {
        return;
    }
    const auto& img = browser_->DisplayImage();
    const int w = img.Width();
    const int h = img.Height();
    const std::size_t px = static_cast<std::size_t>(w) * h;
    bgra_.resize(px * 4);
    const uint8_t* s = img.Data();
    uint8_t* d = bgra_.data();
    for (std::size_t i = 0; i < px; ++i) {
        d[i * 4 + 0] = s[i * 4 + 2];  // B
        d[i * 4 + 1] = s[i * 4 + 1];  // G
        d[i * 4 + 2] = s[i * 4 + 0];  // R
        d[i * 4 + 3] = s[i * 4 + 3];  // A
    }
    uploaded_rev_ = browser_->Revision();
    uploaded_path_ = browser_->CurrentPath();
}

void PhotoViewerWidget::UpdateStatusText() {
    if (!browser_->IsOpen()) {
        status_line1_ = "No image";
        status_line2_ = "Open an image: preview.exe <file>";
        return;
    }
    char buf[640];
    const auto& path = browser_->CurrentPath();
    const auto slash = path.find_last_of("/\\");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const int idx = static_cast<int>(browser_->CurrentIndex() + 1);
    const int total = static_cast<int>(browser_->Files().size());
    char dims[64] = "";
    if (browser_->IsLoaded()) {
        std::snprintf(dims, sizeof(dims), "%dx%d", browser_->ImageWidth(),
                      browser_->ImageHeight());
    }
    const float zoom_pct = FitScale() > 0 ? zoom_ * 100.0f : 0.0f;
    std::snprintf(buf, sizeof(buf), "%s  |  %s  |  %d/%d  |  %.0f%% fit%s",
                  name.c_str(), dims, idx, total, zoom_pct,
                  browser_->IsDirty() ? "  *" : "");
    status_line1_ = buf;
    if (!browser_->StatusMessage().empty()) {
        status_line1_ += "  - " + browser_->StatusMessage();
    }
    status_line2_ =
        "Wheel: zoom | Drag: pan | PgUp/PgDn, Ctrl+N/P: navigate | "
        "Left/Right at fit: prev/next | Crop: drag, Apply to confirm | "
        "Ctrl+F/V: flip | Ctrl+X: rotate | Ctrl+Z: revert | Esc: cancel";
}

void PhotoViewerWidget::SyncFromBrowser() {
    // Detect image change -> reset view + title + filmstrip once per file.
    const std::string cur = browser_->CurrentPath();
    const int w = browser_->ImageWidth();
    const int h = browser_->ImageHeight();
    if (cur != last_path_ || w != last_w_ || h != last_h_) {
        last_path_ = cur;
        last_w_ = w;
        last_h_ = h;
        ResetView();
        ScrollStripToCurrent();
        if (window_ != nullptr) {
            window_->SetTitle("XER Preview - " + BaseName(cur));
        }
    }
}

void PhotoViewerWidget::Render(XER::UI::Rendering::Renderer& renderer,
                               const XER::UI::Theme::Tokens& tokens) {
    SyncFromBrowser();
    RebuildUpload();
    EnsureStripRequests();

    renderer.FillRect(bounds_, Hex(0x101116), 0.0f);
    renderer.FillRect(viewport_, Hex(0x17181F), 8.0f);

    if (browser_->IsLoaded() && !bgra_.empty()) {
        const Rect d = ImageDestRect();
        renderer.PushClip(viewport_);
        renderer.DrawFrameBgra(bgra_.data(), browser_->ImageWidth(),
                               browser_->ImageHeight(), d, 1.0f);
        renderer.PopClip();

        // Border keeps the image edge visible on dark backgrounds.
        renderer.StrokeRect(d, Hex(0x3A3D4A, 0.9f), 1.0f, 0.0f);

        if (crop_mode_) {
            // Crop mode: subtle accent wash + hint.
            renderer.FillRect(viewport_, Hex(0x4DA3FF, 0.05f), 8.0f);
            XER::UI::Rendering::TextStyle hint;
            hint.color = Hex(0x9DB8D9);
            hint.size = 12.5f;
            hint.weight = 500;
            renderer.DrawText("Drag to select the crop area - Apply to confirm, Esc to cancel",
                              Rect{viewport_.x, viewport_.y + 8.0f,
                                   viewport_.width, 18.0f},
                              hint);
            if (crop_rect_.width > 1 && crop_rect_.height > 1) {
                renderer.FillRect(crop_rect_, Hex(0xFFFFFF, 0.12f), 0.0f);
                renderer.StrokeRect(crop_rect_, Hex(0x4DA3FF, 1.0f), 1.5f, 0.0f);
            }
        }
    } else {
        XER::UI::Rendering::TextStyle style;
        style.color = Hex(0x8A8F9E);
        style.size = 15.0f;
        const std::string msg =
            browser_->IsOpen() ? "Loading / unsupported format"
                               : "No image loaded";
        renderer.DrawText(msg,
                          Rect{viewport_.x, viewport_.y + viewport_.height * 0.45f,
                               viewport_.width, 24.0f},
                          style);
    }

    RenderFilmstrip(renderer);

    // Status bar.
    const float sy = bounds_.y + bounds_.height - kStatusH;
    renderer.FillRect(Rect{bounds_.x, sy, bounds_.width, kStatusH},
                      Hex(0x0D0E12), 0.0f);
    renderer.DrawLine(Point{bounds_.x, sy}, Point{bounds_.x + bounds_.width, sy},
                      Hex(0x262932), 1.0f);
    XER::UI::Rendering::TextStyle s1;
    s1.color = Hex(0xE6E8EE);
    s1.size = 12.5f;
    s1.weight = 500;
    renderer.DrawText(status_line1_,
                      Rect{bounds_.x + kPad, sy + 5.0f, bounds_.width - 2 * kPad,
                           16.0f},
                      s1);
    XER::UI::Rendering::TextStyle s2;
    s2.color = Hex(0x7C8192);
    s2.size = 11.0f;
    renderer.DrawText(status_line2_,
                      Rect{bounds_.x + kPad, sy + 20.0f, bounds_.width - 2 * kPad,
                           14.0f},
                      s2);

    (void)tokens;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool PhotoViewerWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::KeyCode;
    using XER::UI::Widgets::MouseButton;

    const bool in_strip = strip_.Contains(event.position);

    switch (event.type) {
        case Event::Type::MouseWheel:
            if (in_strip) {
                // Filmstrip scroll: ~half a cell per wheel notch.
                strip_scroll_ -= event.wheel_delta * kCellW * 0.004f;
                const float max_scroll =
                    6.0f + browser_->Files().size() * kCellW - (strip_.width - 8.0f);
                strip_scroll_ = std::clamp(strip_scroll_, 0.0f,
                                           std::max(0.0f, max_scroll));
                Invalidate();
                return true;
            }
            if (ViewportContains(event.position)) {
                const float factor = event.wheel_delta > 0 ? 1.15f : 1.0f / 1.15f;
                ZoomAt(event.position, factor);
                UpdateStatusText();
                Invalidate();
                return true;
            }
            return false;

        case Event::Type::MouseMove:
            if (strip_drag_) {
                strip_scroll_ += strip_drag_last_.x - event.position.x;
                strip_drag_last_ = event.position;
                strip_moved_ = strip_moved_ ||
                    std::abs(event.position.x - strip_press_.x) > 4.0f ||
                    std::abs(event.position.y - strip_press_.y) > 4.0f;
                const float max_scroll =
                    6.0f + browser_->Files().size() * kCellW - (strip_.width - 8.0f);
                strip_scroll_ = std::clamp(strip_scroll_, 0.0f,
                                           std::max(0.0f, max_scroll));
                Invalidate();
                return true;
            }
            if (in_strip) {
                const int idx = StripIndexAt(event.position);
                if (idx != strip_hover_) {
                    strip_hover_ = idx;
                    Invalidate();
                }
                return true;
            }
            if (strip_hover_ != -1) {
                strip_hover_ = -1;
                Invalidate();
            }
            if (crop_drag_) {
                const float x0 = std::min(crop_start_.x, event.position.x);
                const float y0 = std::min(crop_start_.y, event.position.y);
                const float x1 = std::max(crop_start_.x, event.position.x);
                const float y1 = std::max(crop_start_.y, event.position.y);
                const float cx0 = std::clamp(x0, viewport_.x, viewport_.x + viewport_.width);
                const float cy0 = std::clamp(y0, viewport_.y, viewport_.y + viewport_.height);
                const float cx1 = std::clamp(x1, viewport_.x, viewport_.x + viewport_.width);
                const float cy1 = std::clamp(y1, viewport_.y, viewport_.y + viewport_.height);
                crop_rect_ = {cx0, cy0, cx1 - cx0, cy1 - cy0};
                btn_apply_->SetEnabled(crop_rect_.width >= 4 && crop_rect_.height >= 4);
                Invalidate();
                return true;
            }
            if (panning_) {
                pan_x_ += event.position.x - pan_last_.x;
                pan_y_ += event.position.y - pan_last_.y;
                pan_last_ = event.position;
                Invalidate();
                return true;
            }
            return false;

        case Event::Type::MouseDown:
            if (in_strip) {
                strip_drag_ = true;
                strip_moved_ = false;
                strip_press_ = event.position;
                strip_drag_last_ = event.position;
                return true;
            }
            if (!ViewportContains(event.position)) return false;
            if (event.mouse_button == MouseButton::Middle ||
                (event.mouse_button == MouseButton::Left && !crop_mode_)) {
                panning_ = true;
                pan_last_ = event.position;
                return true;
            }
            if (event.mouse_button == MouseButton::Left && crop_mode_) {
                crop_drag_ = true;
                crop_start_ = event.position;
                crop_rect_ = {event.position.x, event.position.y, 0.0f, 0.0f};
                return true;
            }
            return false;

        case Event::Type::MouseUp:
            if (strip_drag_) {
                strip_drag_ = false;
                if (!strip_moved_) {
                    const int idx = StripIndexAt(event.position);
                    if (idx >= 0 &&
                        static_cast<std::size_t>(idx) != browser_->CurrentIndex()) {
                        browser_->GoToIndex(static_cast<std::size_t>(idx));
                        ExitCropMode();
                        ResetView();
                        UpdateStatusText();
                    }
                }
                Invalidate();
                return true;
            }
            if (crop_drag_) {
                crop_drag_ = false;
                btn_apply_->SetEnabled(crop_rect_.width >= 4 && crop_rect_.height >= 4);
                Invalidate();
                return true;
            }
            if (panning_) {
                panning_ = false;
                return true;
            }
            return false;

        case Event::Type::KeyDown:
            switch (event.key) {
                case KeyCode::PageUp:
                    Navigate(-1);
                    return true;
                case KeyCode::PageDown:
                    Navigate(1);
                    return true;
                case KeyCode::Home:
                    if (browser_->GoToIndex(0)) { ResetView(); UpdateStatusText(); Invalidate(); }
                    return true;
                case KeyCode::End:
                    if (browser_->GoToIndex(browser_->Files().size() - 1)) {
                        ResetView(); UpdateStatusText(); Invalidate();
                    }
                    return true;
                case KeyCode::Escape:
                    if (crop_mode_) { ExitCropMode(); Invalidate(); }
                    return true;
                case KeyCode::ArrowLeft:
                case KeyCode::ArrowRight:
                case KeyCode::ArrowUp:
                case KeyCode::ArrowDown: {
                    const bool horizontal =
                        event.key == KeyCode::ArrowLeft || event.key == KeyCode::ArrowRight;
                    const int dir = (event.key == KeyCode::ArrowLeft ||
                                     event.key == KeyCode::ArrowUp) ? -1 : 1;
                    if (horizontal && zoom_ <= 1.01f) {
                        Navigate(dir);
                    } else {
                        const float step = 32.0f;
                        pan_x_ += horizontal ? dir * step : 0.0f;
                        pan_y_ += !horizontal ? dir * step : 0.0f;
                        Invalidate();
                    }
                    return true;
                }
                case KeyCode::N:
                    if (event.is_ctrl) { Navigate(1); return true; }
                    break;
                case KeyCode::P:
                    if (event.is_ctrl) { Navigate(-1); return true; }
                    break;
                case KeyCode::F:
                    if (event.is_ctrl) { browser_->FlipHorizontal(); ResetView(); Invalidate(); return true; }
                    break;
                case KeyCode::V:
                    if (event.is_ctrl) { browser_->FlipVertical(); ResetView(); Invalidate(); return true; }
                    break;
                case KeyCode::X:
                    if (event.is_ctrl) { browser_->RotateCW(); ResetView(); Invalidate(); return true; }
                    break;
                case KeyCode::Z:
                    if (event.is_ctrl) { browser_->Revert(); ResetView(); UpdateStatusText(); Invalidate(); return true; }
                    break;
                default:
                    break;
            }
            return false;

        default:
            return false;
    }
}

}  // namespace preview
