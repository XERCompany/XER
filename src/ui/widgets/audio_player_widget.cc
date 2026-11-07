#include "audio_player_widget.h"

#include <cmath>
#include <sstream>
#include <iomanip>

#include "app/app_audio_controller.h"
#include "audio/effects_rack/custom_effect.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/switch.h"
#include "layout/geometry.h"

namespace XER::UI::Widgets {

namespace {

std::string FormatTime(double seconds) {
    if (seconds < 0) return "0:00";
    
    int total_seconds = static_cast<int>(seconds);
    int minutes = total_seconds / 60;
    int secs = total_seconds % 60;
    
    std::ostringstream oss;
    oss << minutes << ":" << std::setw(2) << std::setfill('0') << secs;
    return oss.str();
}

} // namespace

AudioPlayerWidget::AudioPlayerWidget(App::AudioController* controller)
    : controller_(controller)
    , current_time_(0.0)
    , duration_(0.0)
    , is_playing_(false)
    , effect_enabled_(false)
    , current_effect_index_(0) {

    // Create child widgets with appropriate styles
    play_pause_button_ = std::make_shared<Button>("Play", ButtonStyle::Primary);
    stop_button_ = std::make_shared<Button>("Stop", ButtonStyle::Secondary);
    mute_button_ = std::make_shared<Button>("Mute", ButtonStyle::Outline);
    seek_slider_ = std::make_shared<Slider>(0.0f, 1.0f, 0.0f);
    volume_slider_ = std::make_shared<Slider>(0.0f, 1.0f, 1.0f);
    time_label_ = std::make_shared<Label>("0:00");
    duration_label_ = std::make_shared<Label>("0:00");
    effect_switch_ = std::make_shared<Switch>(false);
    effect_label_ = std::make_shared<Label>("Effect: None");

    // Set callbacks
    play_pause_button_->SetOnClick([this]() { OnPlayPause(); });
    stop_button_->SetOnClick([this]() { OnStop(); });
    mute_button_->SetOnClick([this]() { OnMute(); });
    seek_slider_->SetOnValueChanged([this](float value) { OnSeek(value); });
    volume_slider_->SetOnValueChanged([this](float value) { OnVolumeChanged(value); });
    effect_switch_->SetOnChanged([this](bool enabled) { OnEffectToggle(enabled); });

    // Load available effects
    LoadAvailableEffects();

    // Set position callback for live updates
    if (controller_) {
        controller_->SetPositionCallback([this](double current, double dur) {
            live_position_.store(current, std::memory_order_relaxed);
            live_duration_.store(dur, std::memory_order_relaxed);
        });
        controller_->SetStateCallback([this](Content::Audio::PlaybackState state) {
            live_state_.store(static_cast<int>(state), std::memory_order_relaxed);
        });
    }
}

void AudioPlayerWidget::EnsureChildrenAttached() {
    if (children_attached_) {
        return;
    }
    children_attached_ = true;

    AddChild(play_pause_button_);
    AddChild(stop_button_);
    AddChild(mute_button_);
    AddChild(seek_slider_);
    AddChild(volume_slider_);
    AddChild(time_label_);
    AddChild(duration_label_);
    AddChild(effect_switch_);
    AddChild(effect_label_);
}

void AudioPlayerWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    EnsureChildrenAttached();
    SyncPlaybackState();

    // Render background
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_medium);
}

void AudioPlayerWidget::Layout(const Layout::Rect& bounds) {
    // Default layout without theme tokens
    Layout(bounds, Theme::Tokens{});
}

void AudioPlayerWidget::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    EnsureChildrenAttached();
    SetBounds(bounds);

    const float padding = tokens.spacing_large;
    const float spacing = tokens.spacing_small;
    const float button_height = 32.0f;
    const float slider_height = 20.0f;
    const float label_width = 50.0f;

    float y = padding;
    float x = padding;
    const float content_width = bounds.width - 2.0f * padding;

    // First row: playback controls and seek slider
    // Play/Pause button
    Layout::Rect play_pause_bounds{x, y, 80.0f, button_height};
    play_pause_button_->Layout(play_pause_bounds);
    x += 80.0f + spacing;

    // Stop button
    Layout::Rect stop_bounds{x, y, 60.0f, button_height};
    stop_button_->Layout(stop_bounds);
    x += 60.0f + spacing;

    // Mute button
    Layout::Rect mute_bounds{x, y, 60.0f, button_height};
    mute_button_->Layout(mute_bounds);
    x += 60.0f + spacing;

    // Time label
    Layout::Rect time_bounds{x, y, label_width, button_height};
    time_label_->Layout(time_bounds);
    x += label_width + spacing;

    // Seek slider (takes remaining space)
    float seek_width = content_width - (80.0f + 60.0f + 60.0f + label_width + spacing * 4);
    Layout::Rect seek_bounds{x, y + (button_height - slider_height) / 2.0f, seek_width, slider_height};
    seek_slider_->Layout(seek_bounds);

    // Duration label (to the right of seek slider)
    Layout::Rect duration_bounds{x + seek_width + spacing, y, label_width, button_height};
    duration_label_->Layout(duration_bounds);

    // Second row: volume slider and effect controls
    y += button_height + spacing;
    x = padding;

    float volume_width = content_width * 0.4f;
    Layout::Rect volume_bounds{x, y + (button_height - slider_height) / 2.0f, volume_width, slider_height};
    volume_slider_->Layout(volume_bounds);
    x += volume_width + spacing * 2;

    // Effect switch
    Layout::Rect effect_switch_bounds{x, y, 40.0f, button_height};
    effect_switch_->Layout(effect_switch_bounds);
    x += 40.0f + spacing;

    // Effect label
    float effect_label_width = content_width - volume_width - 40.0f - spacing * 4;
    Layout::Rect effect_label_bounds{x, y, effect_label_width, button_height};
    effect_label_->Layout(effect_label_bounds);
}

bool AudioPlayerWidget::HandleEvent(const Event&) {
    return false;
}

Layout::Size AudioPlayerWidget::GetMinSize() const {
    return {400.0f, 130.0f};
}

Layout::Size AudioPlayerWidget::GetPreferredSize() const {
    return {600.0f, 150.0f};
}

Layout::Size AudioPlayerWidget::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void AudioPlayerWidget::SyncPlaybackState() {
    is_playing_ = live_state_.load(std::memory_order_relaxed) ==
        static_cast<int>(Content::Audio::PlaybackState::kPlaying);
    UpdateLivePosition(
        live_position_.load(std::memory_order_relaxed),
        live_duration_.load(std::memory_order_relaxed));
}

void AudioPlayerWidget::UpdateLivePosition(double current_time, double duration) {
    current_time_ = current_time;
    duration_ = duration;

    // Update time labels
    time_label_->SetText(FormatTime(current_time));
    duration_label_->SetText(FormatTime(duration));

    // Update seek slider (only if not being dragged by user)
    if (duration > 0) {
        syncing_seek_ = true;
        seek_slider_->SetValue(static_cast<float>(current_time / duration));
        syncing_seek_ = false;
    }

    // Update play/pause button text
    UpdateButtonStates();
}

void AudioPlayerWidget::OnPlayPause() {
    if (!controller_) return;

    if (is_playing_) {
        controller_->Pause();
        is_playing_ = false;
    } else {
        controller_->Play();
        is_playing_ = true;
    }

    UpdateButtonStates();
}

void AudioPlayerWidget::OnStop() {
    if (!controller_) return;

    controller_->Stop();
    is_playing_ = false;
    current_time_ = 0.0;
    live_position_.store(0.0, std::memory_order_relaxed);
    live_duration_.store(duration_, std::memory_order_relaxed);

    UpdateButtonStates();
    UpdateLivePosition(0.0, duration_);
}

void AudioPlayerWidget::OnSeek(float value) {
    if (syncing_seek_ || !controller_ || duration_ <= 0) return;

    double target_time = value * duration_;
    controller_->Seek(target_time);
}

void AudioPlayerWidget::OnVolumeChanged(float value) {
    if (!controller_) return;

    controller_->SetVolume(value);
}

void AudioPlayerWidget::OnMute() {
    if (!controller_) return;

    bool currently_muted = controller_->IsMuted();
    controller_->Mute(!currently_muted);

    // Update button text
    mute_button_->SetText(!currently_muted ? "Unmute" : "Mute");
}

void AudioPlayerWidget::UpdateButtonStates() {
    if (!controller_) return;

    // Update play/pause button text
    if (is_playing_) {
        play_pause_button_->SetText("Pause");
    } else {
        play_pause_button_->SetText("Play");
    }

    // Update mute button text
    bool muted = controller_->IsMuted();
    mute_button_->SetText(muted ? "Unmute" : "Mute");
}

void AudioPlayerWidget::OnEffectToggle(bool enabled) {
    if (!controller_) return;

    effect_enabled_ = enabled;

    if (enabled && !available_effects_.empty()) {
        // Apply the current effect
        std::string effect_name = available_effects_[current_effect_index_];
        auto package = Engine::Audio::FX::BuildNamedCustomEffectPackage(effect_name);

        std::string error;
        if (controller_->ApplyEffectLive(package, &error)) {
            effect_label_->SetText("Effect: " + effect_name);
        } else {
            effect_label_->SetText("Effect: Error");
            effect_switch_->SetChecked(false);
            effect_enabled_ = false;
        }
    } else {
        // Disable effect
        controller_->DisableEffectLive();
        effect_label_->SetText("Effect: None");
    }
}

void AudioPlayerWidget::LoadAvailableEffects() {
    available_effects_ = Engine::Audio::FX::ListAvailableCustomEffects();
    
    if (!available_effects_.empty()) {
        effect_label_->SetText("Effect: " + available_effects_[0]);
    }
}

} // namespace XER::UI::Widgets
