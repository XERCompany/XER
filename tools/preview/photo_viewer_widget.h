#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/widgets/widget.h"
#include "photo_browser.h"
#include "thumb_cache.h"

namespace XER::UI::Widgets {
class AppTitleBar;
class Button;
class Slider;
class Label;
}

namespace XER::UI::Core {
class Window;
}

namespace preview {

/// Photo viewer/editor widget: toolbar, color sliders, image viewport with
/// zoom/pan/crop, a thumbnail filmstrip of the folder, and a status bar.
/// All edits go through PhotoBrowser; thumbnails come from ThumbCache.
class PhotoViewerWidget final : public XER::UI::Widgets::Widget {
public:
    /// `window` is optional — used to update the title bar text on navigation.
    explicit PhotoViewerWidget(PhotoBrowser* browser,
                               XER::UI::Core::Window* window = nullptr);

    /// Frameless-window title bar with File/Info menus (AppTitleBar).
    XER::UI::Widgets::AppTitleBar* TitleBar() const { return title_bar_.get(); }
    /// Called by the File > Quit menu item; wire to Window::Close in main.
    void SetOnQuitRequest(std::function<void()> callback) {
        on_quit_ = std::move(callback);
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    void Layout(const XER::UI::Layout::Rect& bounds,
                const XER::UI::Theme::Tokens& tokens) override;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;

    XER::UI::Layout::Size GetMinSize() const override;
    XER::UI::Layout::Size GetPreferredSize() const override;
    XER::UI::Layout::Size GetMaxSize() const override;

private:
    using Rect = XER::UI::Layout::Rect;
    using Point = XER::UI::Layout::Point;
    using Button = XER::UI::Widgets::Button;
    using Slider = XER::UI::Widgets::Slider;
    using Label = XER::UI::Widgets::Label;

    // App title bar (menus, avatar, window controls).
    void BuildTitleBar();
    void ShowAboutDialog();
    void ShowImageInfoDialog();
    void ShowShortcutsDialog();
    void OpenImageDialog();

    void PositionChildren(const Rect& bounds,
                          const XER::UI::Theme::Tokens& tokens);
    void SyncFromBrowser();

    float FitScale() const;
    Rect ImageDestRect() const;
    bool ViewportContains(Point p) const { return viewport_.Contains(p); }

    void ZoomAt(Point center, float factor);
    void ResetView();
    void EnterCropMode();
    void ExitCropMode();
    void ApplyCrop();
    void Navigate(int delta);

    void RebuildUpload();
    void UpdateStatusText();

    // Filmstrip.
    void EnsureStripRequests();
    void ScrollStripToCurrent();
    float StripMaxScroll() const;
    Rect StripCellRect(std::size_t index) const;
    int StripIndexAt(Point p) const;
    void RenderFilmstrip(XER::UI::Rendering::Renderer& renderer,
                         const XER::UI::Theme::Tokens& tokens);

    PhotoBrowser* browser_;
    XER::UI::Core::Window* window_ = nullptr;
    std::unique_ptr<ThumbCache> thumbs_;
    std::shared_ptr<XER::UI::Widgets::AppTitleBar> title_bar_;
    std::function<void()> on_quit_;

    // Child widgets.
    std::shared_ptr<Button> btn_prev_, btn_next_, btn_rot_cw_, btn_rot_ccw_;
    std::shared_ptr<Button> btn_flip_h_, btn_flip_v_, btn_crop_, btn_fit_;
    std::shared_ptr<Button> btn_apply_, btn_cancel_, btn_save_, btn_png_,
        btn_revert_;
    std::shared_ptr<Slider> slider_bright_, slider_contrast_;
    std::shared_ptr<Label> label_bright_, label_contrast_;

    Rect bounds_{};
    Rect toolbar_panel_{};
    Rect editbar_panel_{};
    Rect viewport_{};
    Rect strip_{};
    // X positions of toolbar group separators (computed during layout).
    float sep_xs_[2] = {0.0f, 0.0f};

    // View transform: image pixels -> widget coords via dest rect.
    float zoom_ = 1.0f;   // multiplier on fit scale
    float pan_x_ = 0.0f;  // dest rect top-left, absolute coords
    float pan_y_ = 0.0f;

    // Upload cache (RGBA -> BGRA for DrawFrameBgra).
    std::vector<uint8_t> bgra_;
    std::uint64_t uploaded_rev_ = ~std::uint64_t{0};
    std::string uploaded_path_;

    // Crop interaction.
    bool crop_mode_ = false;
    bool crop_drag_ = false;
    Point crop_start_{};
    Rect crop_rect_{};   // absolute widget coords

    // Pan interaction.
    bool panning_ = false;
    Point pan_last_{};

    // Filmstrip interaction.
    float strip_scroll_ = 0.0f;   // horizontal scroll offset in px
    int strip_hover_ = -1;        // hovered cell index or -1
    bool strip_drag_ = false;
    bool strip_moved_ = false;
    Point strip_drag_last_{};
    Point strip_press_{};

    std::string status_line1_;
    std::string status_line2_;

    // Last-seen image identity for one-shot view resets.
    std::string last_path_;
    int last_w_ = -1;
    int last_h_ = -1;
};

}  // namespace preview
