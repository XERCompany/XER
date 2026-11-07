#ifndef XER_UI_WIDGETS_VIDEO_PREVIEW_WIDGET_H
#define XER_UI_WIDGETS_VIDEO_PREVIEW_WIDGET_H

#include <cstdint>
#include <memory>
#include <string>

#include "content/video/video_engine.h"
#include "content/video/video_frame.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Button;
class Label;
class Slider;
class Switch;
class TextInput;

// Video preview panel: file picker + transport controls on top, the decoded
// frame letterboxed below. All decode/playback/audio work lives in
// XER::Content::Video::VideoEngine — this widget only pumps Tick(), mirrors
// transport state into chrome, and draws the newest frame through
// Renderer::DrawFrameBgra (GPU staging bitmap on the D2D target).
class VideoPreviewWidget final : public Widget {
public:
    // parent_hwnd: HWND of the window hosting the native file dialogs.
    explicit VideoPreviewWidget(void* parent_hwnd);
    ~VideoPreviewWidget() override;

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds,
                const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override { return {360.0f, 240.0f}; }
    Layout::Size GetPreferredSize() const override {
        return {720.0f, 460.0f};
    }
    Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

    // Host hook: pause/resume decode when the containing view is hidden —
    // avoids decoding video nobody can see.
    void SetPreviewActive(bool active);

private:
    void EnsureChildren();
    void OnBrowse();
    void OnLoad();
    void OnTogglePlay();
    void RefreshTransportUi();
    void SetStatus(const std::string& text, bool is_error = false);

    void* parent_hwnd_ = nullptr;
    std::unique_ptr<XER::Content::Video::VideoEngine> engine_;

    std::shared_ptr<TextInput> path_input_;
    std::shared_ptr<Button> browse_btn_;
    std::shared_ptr<Button> load_btn_;
    std::shared_ptr<Button> play_btn_;
    std::shared_ptr<Button> stop_btn_;
    std::shared_ptr<Slider> seek_slider_;
    std::shared_ptr<Label> time_label_;
    std::shared_ptr<Label> status_label_;
    std::shared_ptr<Switch> loop_switch_;

    // Draw state — updated by Tick(), consumed by Render().
    XER::Content::Video::VideoFramePtr draw_frame_;
    XER::Content::Video::VideoFramePtr poster_frame_;
    std::uint64_t drawn_seq_ = 0;
    Layout::Rect video_area_{};
    Layout::Rect video_dest_{};
    bool have_dest_ = false;

    bool children_attached_ = false;
    bool preview_active_ = true;
    bool was_playing_before_hide_ = false;
    // Slider::SetValue fires on_value_changed for programmatic updates too —
    // this flag suppresses the echo so position sync doesn't trigger Seek().
    bool syncing_seek_ = false;
    double ui_refresh_acc_ = 0.0;
    bool status_dirty_ = true;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_VIDEO_PREVIEW_WIDGET_H
