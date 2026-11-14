#include "photo_viewer_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include "ui/core/window.h"
#include "ui/rendering/renderer.h"
#include "ui/theme/tokens.h"
#include "ui/widgets/app_title_bar.h"
#include "ui/widgets/button.h"
#include "ui/widgets/dialog.h"
#include "ui/widgets/label.h"
#include "ui/widgets/slider.h"

namespace preview {

// Out-of-class definitions cannot see the private member aliases; import the
// layout types at namespace scope for signatures.
using XER::UI::Layout::Rect;
using XER::UI::Layout::Point;
using XER::UI::Layout::Size;

namespace {

constexpr float kTitleBarH = 38.0f;
constexpr float kToolbarH = 46.0f;
constexpr float kEditbarH = 56.0f;
constexpr float kStripH = 122.0f;
constexpr float kStatusH = 36.0f;
constexpr float kPad = 12.0f;
constexpr float kGap = 8.0f;
constexpr float kBtnH = 30.0f;
constexpr float kBtnW = 82.0f;

// Filmstrip cell geometry.
constexpr float kCellW = 148.0f;
constexpr float kCellH = 106.0f;
constexpr float kThumbW = 128.0f;
constexpr float kThumbH = 86.0f;
constexpr float kCellPadX = (kCellW - kThumbW) * 0.5f;
constexpr float kCellPadY = (kCellH - kThumbH) * 0.5f;

XER::UI::Theme::Color WithAlpha(XER::UI::Theme::Color c, float a) {
    c.a = a;
    return c;
}

std::string BaseName(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

/// Multiline read-only text used as Dialog content.
class DialogText final : public XER::UI::Widgets::Widget {
public:
    explicit DialogText(std::string text) : text_(std::move(text)) {}

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override {
        XER::UI::Rendering::TextStyle style;
        style.color = tokens.foreground;
        style.size = tokens.font_body;
        style.weight = 400;
        renderer.DrawText(text_, GetBounds(), style);
    }
    void Layout(const Rect& bounds) override { SetBounds(bounds); }
    bool HandleEvent(const XER::UI::Widgets::Event&) override { return false; }
    Size GetMinSize() const override { return {0.0f, 0.0f}; }
    Size GetPreferredSize() const override {
        float lines = 1.0f;
        for (char c : text_) lines += c == '\n' ? 1.0f : 0.0f;
        return {380.0f, lines * 20.0f + 8.0f};
    }
    Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

private:
    std::string text_;
};

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

    // shadcn mix: navigation = outline, transforms = secondary,
    // file actions = primary/ghost.
    btn_prev_ = make_btn("< Prev", ButtonStyle::Outline);
    btn_next_ = make_btn("Next >", ButtonStyle::Outline);
    btn_rot_cw_ = make_btn("Rotate CW");
    btn_rot_ccw_ = make_btn("Rotate CCW");
    btn_flip_h_ = make_btn("Flip H");
    btn_flip_v_ = make_btn("Flip V");
    btn_crop_ = make_btn("Crop");
    btn_fit_ = make_btn("Fit", ButtonStyle::Ghost);

    btn_apply_ = make_btn("Apply", ButtonStyle::Primary);
    btn_cancel_ = make_btn("Cancel", ButtonStyle::Ghost);
    btn_save_ = make_btn("Save", ButtonStyle::Primary);
    btn_png_ = make_btn("PNG", ButtonStyle::Outline);
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
    BuildTitleBar();
}

// ---------------------------------------------------------------------------
// App title bar (File / Info menus)
// ---------------------------------------------------------------------------

void PhotoViewerWidget::BuildTitleBar() {
    using XER::UI::Widgets::AppTitleBar;
    using Menu = AppTitleBar::Menu;
    using MenuItem = AppTitleBar::MenuItem;

    title_bar_ = std::make_shared<AppTitleBar>("XER Preview");
    auto* tb = title_bar_.get();
    AddChild(title_bar_);

    tb->SetWorkspaceLabel("Photo Browser");
    tb->SetCenterCommand("Current image", nullptr, false);

    Menu file;
    file.label = "File";
    file.mnemonic = 'F';
    file.items = {
        {"Open Image...", "Ctrl+O", [this] { OpenImageDialog(); }},
        {"Previous Image", "PgUp", [this] { Navigate(-1); }},
        {"Next Image", "PgDn", [this] { Navigate(1); }},
        {"Save", "Ctrl+S", [this] {
            browser_->Save();
            UpdateStatusText();
            Invalidate();
        }, true, false, false, true},
        {"Export PNG...", "", [this] {
            browser_->ExportPng();
            UpdateStatusText();
            Invalidate();
        }},
        {"Quit", "Alt+F4", [this] {
            if (on_quit_) on_quit_();
        }, true, false, false, true},
    };

    Menu info;
    info.label = "Info";
    info.mnemonic = 'I';
    info.items = {
        {"About XER Preview", "", [this] { ShowAboutDialog(); }},
        {"Image Info", "", [this] { ShowImageInfoDialog(); },
         true, false, false, true},
        {"Keyboard Shortcuts", "", [this] { ShowShortcutsDialog(); }},
    };

    tb->SetMenus({std::move(file), std::move(info)});
    tb->SetAvatar("Yoshi Arasaka", {
        {"About XER Preview", "", [this] { ShowAboutDialog(); }},
    });
}

void PhotoViewerWidget::ShowAboutDialog() {
    auto text = std::make_shared<DialogText>(
        "XER Preview\n"
        "Native photo browser & editor\n\n"
        "Author: Yoshi Arasaka\n\n"
        "Built on src/ui + src/image (XER3)");
    auto dlg = XER::UI::Widgets::Dialog::Create("About", text, "OK");
    dlg->SetPanelWidth(400.0f);
    ShowOverlay(dlg);
}

void PhotoViewerWidget::ShowImageInfoDialog() {
    char buf[1024];
    const auto& path = browser_->CurrentPath();
    const auto slash = path.find_last_of("/\\");
    const std::string name =
        slash == std::string::npos ? path : path.substr(slash + 1);
    std::snprintf(buf, sizeof(buf),
                  "File: %s\n"
                  "Path: %s\n"
                  "Folder images: %zu (current %zu)\n"
                  "Size: %dx%d\n"
                  "Modified: %s",
                  name.c_str(), path.c_str(),
                  browser_->Files().size(), browser_->CurrentIndex() + 1,
                  browser_->ImageWidth(), browser_->ImageHeight(),
                  browser_->IsDirty() ? "yes (unsaved edits)" : "no");
    auto text = std::make_shared<DialogText>(buf);
    auto dlg = XER::UI::Widgets::Dialog::Create("Image Info", text, "OK");
    dlg->SetPanelWidth(460.0f);
    ShowOverlay(dlg);
}

void PhotoViewerWidget::ShowShortcutsDialog() {
    auto text = std::make_shared<DialogText>(
        "Navigate:   PgUp / PgDn, Ctrl+N / Ctrl+P, Home / End\n"
        "            Left / Right at fit scale\n"
        "Zoom:       Mouse wheel (towards cursor)\n"
        "Pan:        Drag image, arrow keys, middle-drag\n"
        "Filmstrip:  Click to jump, drag or wheel to scroll\n"
        "Crop:       Crop button -> drag a rectangle -> Apply\n"
        "             Esc cancels\n"
        "Rotate:     Ctrl+X (clockwise)\n"
        "Flip:       Ctrl+F / Ctrl+V\n"
        "Revert:     Ctrl+Z (reload from disk)\n"
        "Files:      Ctrl+O open, Ctrl+S save, PNG export button");
    auto dlg = XER::UI::Widgets::Dialog::Create("Keyboard Shortcuts", text,
                                                "OK");
    dlg->SetPanelWidth(460.0f);
    ShowOverlay(dlg);
}

void PhotoViewerWidget::OpenImageDialog() {
#ifdef _WIN32
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = window_ != nullptr ? window_->GetHWND() : nullptr;
    ofn.lpstrFilter =
        L"Images\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.tif;*.tiff;"
        L"*.heic;*.heif;*.avif;*.jxl;*.svg;*.psd;*.exr;*.hdr\0"
        L"All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Open image";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    char utf8[MAX_PATH * 4] = {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, file, -1, utf8,
                                        sizeof(utf8), nullptr, nullptr);
    if (len <= 1) return;
    if (!browser_->Open(utf8)) {
        UpdateStatusText();
        Invalidate();
        return;
    }
    // PhotoBrowser::Open resets edits; resync view state and sliders.
    last_path_.clear();
    last_w_ = last_h_ = -1;
    strip_scroll_ = 0.0f;
    strip_hover_ = -1;
    slider_bright_->SetValue(0.0f);
    slider_contrast_->SetValue(100.0f);
    SyncFromBrowser();
    UpdateStatusText();
    Invalidate();
#else
    // GUI is Windows-only; nothing to do elsewhere.
#endif
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
    const float w = bounds.width - 2 * kPad;

    // Frameless title bar spans the full client width at the top.
    title_bar_->Layout(Rect{bounds.x, bounds.y, bounds.width, kTitleBarH},
                       tokens);

    float cy = bounds.y + kTitleBarH + kGap;

    // --- Card panels (shadcn blocks): toolbar / editbar / viewport / strip ---
    toolbar_panel_ = Rect{x, cy, w, kToolbarH};
    cy += kToolbarH + kGap;

    editbar_panel_ = Rect{x, cy, w, kEditbarH};
    cy += kEditbarH + kGap;

    const float footer_h = kStatusH;
    const float strip_h = kStripH;
    const float vp_bottom =
        bounds.y + bounds.height - footer_h - strip_h - 2 * kGap;
    viewport_ = Rect{x, cy, w, vp_bottom - cy};
    if (viewport_.width < 0) viewport_.width = 0;
    if (viewport_.height < 0) viewport_.height = 0;
    cy = vp_bottom + kGap;

    strip_ = Rect{x, cy, w, strip_h};
    if (strip_.width < 0) strip_.width = 0;
    if (strip_.height < 0) strip_.height = 0;

    // --- Row 1 inside toolbar panel: nav | transforms | view ---
    const float y1 = toolbar_panel_.y + (kToolbarH - kBtnH) * 0.5f;
    const std::vector<std::shared_ptr<Button>> row1 = {
        btn_prev_, btn_next_, btn_rot_cw_, btn_rot_ccw_,
        btn_flip_h_, btn_flip_v_, btn_crop_, btn_fit_,
    };
    float bx = x + 10.0f;
    int sep_i = 0;
    for (size_t i = 0; i < row1.size(); ++i) {
        row1[i]->Layout(Rect{bx, y1, kBtnW, kBtnH}, tokens);
        bx += kBtnW + kGap;
        // Vertical separators between visual groups.
        if ((i == 1 || i == 5) && sep_i < 2) {
            sep_xs_[sep_i++] = bx - kGap * 0.5f;
        }
    }

    // --- Row 2 inside editbar panel: crop actions | sliders | file ops ---
    const float y2 = editbar_panel_.y + (kEditbarH - kBtnH) * 0.5f;
    btn_apply_->Layout(Rect{x + 10.0f, y2, 76.0f, kBtnH}, tokens);
    btn_cancel_->Layout(Rect{x + 92.0f, y2, 76.0f, kBtnH}, tokens);

    label_bright_->Layout(Rect{x + 186.0f, y2 + 7.0f, 56.0f, 16.0f}, tokens);
    slider_bright_->Layout(Rect{x + 244.0f, y2 + 1.0f, 190.0f, kBtnH - 2.0f}, tokens);

    label_contrast_->Layout(Rect{x + 450.0f, y2 + 7.0f, 64.0f, 16.0f}, tokens);
    slider_contrast_->Layout(Rect{x + 516.0f, y2 + 1.0f, 190.0f, kBtnH - 2.0f}, tokens);

    const float fx = x + w - 250.0f;
    btn_save_->Layout(Rect{fx, y2, 72.0f, kBtnH}, tokens);
    btn_png_->Layout(Rect{fx + 78.0f, y2, 64.0f, kBtnH}, tokens);
    btn_revert_->Layout(Rect{fx + 148.0f, y2, 80.0f, kBtnH}, tokens);
}

XER::UI::Layout::Size PhotoViewerWidget::GetMinSize() const {
    return {860.0f, 660.0f};
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
    return {strip_.x + 8.0f + static_cast<float>(index) * kCellW - strip_scroll_,
            strip_.y + 8.0f,
            kCellW - 8.0f,
            kCellH};
}

float PhotoViewerWidget::StripMaxScroll() const {
    // Content spans [strip_.x + 8, strip_.x + n*kCellW]; keep the last cell
    // inside the right padding.
    const float content =
        static_cast<float>(browser_->Files().size()) * kCellW;
    return std::max(0.0f, content - strip_.width + 8.0f);
}

int PhotoViewerWidget::StripIndexAt(Point p) const {
    if (!strip_.Contains(p)) return -1;
    const float rel = p.x - strip_.x - 8.0f + strip_scroll_;
    const int idx = static_cast<int>(rel / kCellW);
    if (idx < 0 || idx >= static_cast<int>(browser_->Files().size())) return -1;
    const Rect cell = StripCellRect(static_cast<std::size_t>(idx));
    return cell.Contains(p) ? idx : -1;
}

void PhotoViewerWidget::ScrollStripToCurrent() {
    const std::size_t cur = browser_->CurrentIndex();
    const Rect cell = StripCellRect(cur);
    const float visible_left = strip_.x + 8.0f;
    const float visible_right = strip_.x + strip_.width - 8.0f;
    if (cell.x < visible_left) {
        strip_scroll_ -= (visible_left - cell.x);
    } else if (cell.x + cell.width > visible_right) {
        strip_scroll_ += (cell.x + cell.width) - visible_right;
    }
    strip_scroll_ = std::clamp(strip_scroll_, 0.0f, StripMaxScroll());
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

void PhotoViewerWidget::RenderFilmstrip(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    if (!browser_->IsOpen()) return;
    const float radius = tokens.radius_large;

    // Clip children so thumbs can't bleed outside the strip.
    renderer.PushClip(strip_);
    renderer.FillRect(strip_, tokens.surface, radius);
    renderer.StrokeRect(strip_, tokens.border, 1.0f, radius);

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

        // Cell background: shadcn ghost-button semantics — transparent idle,
        // `hover` fill on hover, `secondary` for the current item.
        if (current) {
            renderer.FillRect(cell, tokens.secondary, tokens.radius_medium);
        } else if (hovered) {
            renderer.FillRect(cell, tokens.hover, tokens.radius_medium);
        }

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
                                   dst, current ? 1.0f : 0.8f);
        } else {
            // Pending/failed placeholder: subtle `secondary` chip.
            renderer.FillRect(
                Rect{cell.x + kCellPadX, cell.y + kCellPadY, kThumbW, kThumbH},
                tokens.secondary, tokens.radius_small);
        }

        // Border: accent ring for current, border hairline for the rest.
        if (current) {
            renderer.StrokeRect(cell, tokens.accent, 2.0f, tokens.radius_medium);
        } else {
            renderer.StrokeRect(cell, tokens.border, 1.0f, tokens.radius_medium);
        }
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
    char dims[64] = "";
    if (browser_->IsLoaded()) {
        std::snprintf(dims, sizeof(dims), "%dx%d", browser_->ImageWidth(),
                      browser_->ImageHeight());
    }
    std::snprintf(buf, sizeof(buf), "%s  |  %s%s",
                  name.c_str(), dims,
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
        if (title_bar_ != nullptr) {
            title_bar_->SetCenterCommandValue(BaseName(cur));
        }
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

    const float radius = tokens.radius_large;
    renderer.FillRect(bounds_, tokens.background, 0.0f);

    // shadcn card panels: surface fill + hairline border.
    for (const Rect* panel : {&toolbar_panel_, &editbar_panel_, &viewport_}) {
        renderer.FillRect(*panel, tokens.surface, radius);
        renderer.StrokeRect(*panel, tokens.border, 1.0f, radius);
    }

    // Toolbar group separators.
    for (float sx : sep_xs_) {
        if (sx <= 0.0f) continue;
        renderer.DrawLine(
            Point{sx, toolbar_panel_.y + 12.0f},
            Point{sx, toolbar_panel_.y + toolbar_panel_.height - 12.0f},
            tokens.border, 1.0f);
    }

    if (browser_->IsLoaded() && !bgra_.empty()) {
        const Rect d = ImageDestRect();
        renderer.PushClip(viewport_);
        renderer.DrawFrameBgra(bgra_.data(), browser_->ImageWidth(),
                               browser_->ImageHeight(), d, 1.0f);
        renderer.PopClip();

        // Border keeps the image edge visible on light backgrounds too.
        renderer.StrokeRect(d, WithAlpha(tokens.foreground, 0.25f), 1.0f, 0.0f);

        if (crop_mode_) {
            // Crop mode: subtle accent wash + hint.
            renderer.FillRect(viewport_, WithAlpha(tokens.accent, 0.06f), radius);
            XER::UI::Rendering::TextStyle hint;
            hint.color = tokens.muted;
            hint.size = tokens.font_small;
            hint.weight = 500;
            renderer.DrawText("Drag to select the crop area - Apply to confirm, Esc to cancel",
                              Rect{viewport_.x, viewport_.y + 8.0f,
                                   viewport_.width, 18.0f},
                              hint);
            if (crop_rect_.width > 1 && crop_rect_.height > 1) {
                renderer.FillRect(crop_rect_, WithAlpha(tokens.accent_foreground, 0.15f), 0.0f);
                renderer.StrokeRect(crop_rect_, tokens.accent, 1.5f, 0.0f);
            }
        }
    } else {
        XER::UI::Rendering::TextStyle style;
        style.color = tokens.muted;
        style.size = tokens.font_body;
        const std::string msg =
            browser_->IsOpen() ? "Loading / unsupported format"
                               : "No image loaded";
        renderer.DrawText(msg,
                          Rect{viewport_.x, viewport_.y + viewport_.height * 0.45f,
                               viewport_.width, 24.0f},
                          style);
    }

    RenderFilmstrip(renderer, tokens);

    // Status footer: surface strip with a hairline top separator.
    const float sy = bounds_.y + bounds_.height - kStatusH;
    renderer.FillRect(Rect{bounds_.x, sy, bounds_.width, kStatusH},
                      tokens.surface, 0.0f);
    renderer.DrawLine(Point{bounds_.x, sy}, Point{bounds_.x + bounds_.width, sy},
                      tokens.border, 1.0f);
    XER::UI::Rendering::TextStyle s1;
    s1.color = tokens.foreground;
    s1.size = tokens.font_small;
    s1.weight = 500;
    renderer.DrawText(status_line1_,
                      Rect{bounds_.x + kPad, sy + 5.0f,
                           bounds_.width - 2 * kPad - 120.0f, 16.0f},
                      s1);
    XER::UI::Rendering::TextStyle s2;
    s2.color = tokens.muted;
    s2.size = tokens.font_small - 1.0f;
    renderer.DrawText(status_line2_,
                      Rect{bounds_.x + kPad, sy + 20.0f, bounds_.width - 2 * kPad,
                           14.0f},
                      s2);

    // shadcn Badge: position + zoom pill on the right of the footer.
    if (browser_->IsOpen()) {
        char badge[64];
        const float zoom_pct = FitScale() > 0 ? zoom_ * 100.0f : 0.0f;
        std::snprintf(badge, sizeof(badge), "%d/%d | %.0f%%",
                      static_cast<int>(browser_->CurrentIndex() + 1),
                      static_cast<int>(browser_->Files().size()), zoom_pct);
        XER::UI::Rendering::TextStyle bs;
        bs.color = tokens.foreground;
        bs.size = tokens.font_small;
        bs.weight = 500;
        const float text_w = renderer.MeasureText(badge, bs).width;
        const float pill_w = text_w + 18.0f;
        const float pill_h = 20.0f;
        const Rect pill{bounds_.x + bounds_.width - kPad - pill_w,
                        sy + (kStatusH - pill_h) * 0.5f, pill_w, pill_h};
        renderer.FillRect(pill, tokens.secondary, pill_h * 0.5f);
        renderer.DrawText(badge,
                          Rect{pill.x + 9.0f, pill.y + 2.0f, text_w, pill_h - 4.0f},
                          bs);
    }

    // shadcn outer focus ring when the widget has keyboard focus.
    if (IsFocused()) {
        RenderFocusRing(renderer, tokens, tokens.radius_large);
    }
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
                strip_scroll_ = std::clamp(strip_scroll_, 0.0f, StripMaxScroll());
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
                strip_scroll_ = std::clamp(strip_scroll_, 0.0f, StripMaxScroll());
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
                case KeyCode::O:
                    if (event.is_ctrl) { OpenImageDialog(); return true; }
                    break;
                case KeyCode::S:
                    if (event.is_ctrl) {
                        browser_->Save();
                        UpdateStatusText();
                        Invalidate();
                        return true;
                    }
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
