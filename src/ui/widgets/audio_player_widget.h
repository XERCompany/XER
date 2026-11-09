#ifndef XER_UI_WIDGETS_AUDIO_PLAYER_WIDGET_H
#define XER_UI_WIDGETS_AUDIO_PLAYER_WIDGET_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "widgets/widget.h"
#include "widgets/button.h"
#include "widgets/slider.h"
#include "widgets/label.h"
#include "widgets/waveform_widget.h"

namespace XER::App {
class AudioController;
}

namespace XER::UI::Widgets {

// Compact audio transport: play/pause/stop/mute, seek, volume and an "FX"
// button that opens the dedicated FX-rack window (FxRackWidget) via
// SetOnOpenFx. Playback state is polled through Tick() from the window's
// frame callback.
class AudioPlayerWidget final : public Widget {
public:
    explicit AudioPlayerWidget(XER::App::AudioController* controller);
    ~AudioPlayerWidget() override = default;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void UpdateLivePosition(double current_time, double duration);
    // Polls the audio controller and applies state changes. Intended to be
    // called from Window::SetFrameCallback; does NOT invalidate by itself —
    // widget setters only dirty the tree when values actually change.
    void Tick();

    // Called when the "FX" button is pressed (typically shows the FX window).
    void SetOnOpenFx(std::function<void()> callback);
    // Called from the context menu "Open Video Preview" item.
    void SetOnOpenVideoPreview(std::function<void()> callback);

private:
    void OnPlayPause();
    void OnStop();
    void OnSeek(float value);
    void OnVolumeChanged(float value);
    void OnMute();
    void OnSetVolume(float value);
    bool OnContextMenu(const Event& event);
    void UpdateButtonStates();
    void SyncPlaybackState();
    void EnsureChildrenAttached();

    XER::App::AudioController* controller_;

    std::shared_ptr<Button> play_pause_button_;
    std::shared_ptr<Button> stop_button_;
    std::shared_ptr<Button> mute_button_;
    std::shared_ptr<Button> fx_button_;
    std::shared_ptr<Slider> seek_slider_;
    std::shared_ptr<Slider> volume_slider_;
    std::shared_ptr<Label> volume_label_;
    std::shared_ptr<Label> time_label_;
    std::shared_ptr<Label> status_label_;
    std::shared_ptr<WaveformWidget> waveform_;

    double current_time_;
    double duration_;
    bool is_playing_;
    bool syncing_seek_ = false;
    bool children_attached_ = false;
    bool context_menu_registered_ = false;
    std::function<void()> on_open_fx_;
    std::function<void()> on_open_video_;

    std::atomic<double> live_position_{0.0};
    std::atomic<double> live_duration_{0.0};
    std::atomic<int> live_state_{0};
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_AUDIO_PLAYER_WIDGET_H
