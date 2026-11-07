#ifndef XER_UI_WIDGETS_AUDIO_PLAYER_WIDGET_H
#define XER_UI_WIDGETS_AUDIO_PLAYER_WIDGET_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"
#include "widgets/button.h"
#include "widgets/slider.h"
#include "widgets/label.h"
#include "widgets/switch.h"

#include "audio/effects_rack/custom_effect_struct.h"

namespace App {
class AudioController;
}

namespace XER::UI::Widgets {

class AudioPlayerWidget final : public Widget {
public:
    explicit AudioPlayerWidget(App::AudioController* controller);
    ~AudioPlayerWidget() override = default;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    void Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) override;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

    void UpdateLivePosition(double current_time, double duration);

private:
    void OnPlayPause();
    void OnStop();
    void OnSeek(float value);
    void OnVolumeChanged(float value);
    void OnMute();
    void OnEffectToggle(bool enabled);
    void UpdateButtonStates();
    void SyncPlaybackState();
    void EnsureChildrenAttached();
    void LoadAvailableEffects();

    App::AudioController* controller_;

    // Child widgets
    std::shared_ptr<Button> play_pause_button_;
    std::shared_ptr<Button> stop_button_;
    std::shared_ptr<Button> mute_button_;
    std::shared_ptr<Slider> seek_slider_;
    std::shared_ptr<Slider> volume_slider_;
    std::shared_ptr<Label> time_label_;
    std::shared_ptr<Label> duration_label_;
    std::shared_ptr<Switch> effect_switch_;
    std::shared_ptr<Label> effect_label_;

    // State
    double current_time_;
    double duration_;
    bool is_playing_;
    bool effect_enabled_;
    bool syncing_seek_ = false;
    bool children_attached_ = false;

    std::atomic<double> live_position_{0.0};
    std::atomic<double> live_duration_{0.0};
    std::atomic<int> live_state_{0};

    // Available effects
    std::vector<std::string> available_effects_;
    int current_effect_index_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_AUDIO_PLAYER_WIDGET_H
