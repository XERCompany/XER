#include "audio_player_widget.h"

#include <cmath>
#include <sstream>
#include <iomanip>

#include "app/app_audio_controller.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"
#include "widgets/waveform_widget.h"
#include "widgets/widget_tree.h"

namespace XER::UI::Widgets {

namespace {

std::string FormatTime(double seconds) {
    if (seconds < 0) return "0:00";
    int total_seconds = static_cast<int>(seconds);
    std::ostringstream oss;
    oss << (total_seconds / 60) << ":" << std::setw(2) << std::setfill('0') << (total_seconds % 60);
    return oss.str();
}

} // namespace

AudioPlayerWidget::AudioPlayerWidget(XER::App::AudioController* controller)
    : controller_(controller)
    , current_time_(0.0)
    , duration_(0.0)
    , is_playing_(false) {

    play_pause_button_ = std::make_shared<Button>("Play", ButtonStyle::Primary);
    stop_button_ = std::make_shared<Button>("Stop", ButtonStyle::Secondary);
    mute_button_ = std::make_shared<Button>("Mute", ButtonStyle::Outline);
    fx_button_ = std::make_shared<Button>("FX", ButtonStyle::Secondary);
    seek_slider_ = std::make_shared<Slider>(0.0f, 1.0f, 0.0f);
    volume_slider_ = std::make_shared<Slider>(0.0f, 1.0f, 1.0f);
    volume_label_ = std::make_shared<Label>("Vol", true);
    time_label_ = std::make_shared<Label>("0:00 / 0:00", true);
    status_label_ = std::make_shared<Label>("Ready", true);
    waveform_ = std::make_shared<WaveformWidget>();

    play_pause_button_->SetOnClick([this]() { OnPlayPause(); });
    stop_button_->SetOnClick([this]() { OnStop(); });
    mute_button_->SetOnClick([this]() { OnMute(); });
    fx_button_->SetOnClick([this]() {
        if (on_open_fx_) {
            on_open_fx_();
        }
    });
    play_pause_button_->SetTooltip("Play / Pause  (Space)");
    stop_button_->SetTooltip("Stop");
    mute_button_->SetTooltip("Mute / Unmute output");
    fx_button_->SetTooltip("Open FX rack");
    seek_slider_->SetTooltip("Seek position");
    volume_slider_->SetTooltip("Output volume");
    waveform_->SetTooltip("Playback position");
    seek_slider_->SetOnValueChanged([this](float value) { OnSeek(value); });
    volume_slider_->SetOnValueChanged([this](float value) { OnVolumeChanged(value); });

    if (controller_) {
        controller_->SetPositionCallback([this](double current, double dur) {
            live_position_.store(current, std::memory_order_relaxed);
            live_duration_.store(dur, std::memory_order_relaxed);
        });
        controller_->SetStateCallback([this](XER::Content::Audio::PlaybackState state) {
            live_state_.store(static_cast<int>(state), std::memory_order_relaxed);
        });
    }
}

void AudioPlayerWidget::SetOnOpenFx(std::function<void()> callback) {
    on_open_fx_ = std::move(callback);
}

void AudioPlayerWidget::SetOnOpenVideoPreview(std::function<void()> callback) {
    on_open_video_ = std::move(callback);
}

void AudioPlayerWidget::EnsureChildrenAttached() {
    if (children_attached_) {
        return;
    }
    children_attached_ = true;

    AddChild(play_pause_button_);
    AddChild(stop_button_);
    AddChild(mute_button_);
    AddChild(fx_button_);
    AddChild(seek_slider_);
    AddChild(volume_slider_);
    AddChild(volume_label_);
    AddChild(time_label_);
    AddChild(status_label_);
    AddChild(waveform_);

    // Right-click anywhere on the player (including over child controls that
    // ignore non-left buttons) opens the transport context menu.
    if (WidgetTree* tree = GetTree(); tree != nullptr && !context_menu_registered_) {
        context_menu_registered_ = true;
        tree->SetContextMenuHandler([this](const Event& event) {
            return OnContextMenu(event);
        });
        // Global transport shortcuts. Runs only when no focused widget
        // consumed the key, so typing in inputs stays unaffected.
        tree->SetKeyHandler([this](const Event& event) {
            if (event.type == Event::Type::KeyDown &&
                event.key == KeyCode::Space) {
                OnPlayPause();
                return true;
            }
            if (event.type == Event::Type::TextInput &&
                (event.text == "m" || event.text == "M" || event.text == "ь" || event.text == "Ь")) {
                OnMute();
                return true;
            }
            return false;
        });
    }
}

void AudioPlayerWidget::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    EnsureChildrenAttached();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    // Subtle accent tick on the leading edge while playing.
    if (is_playing_) {
        renderer.FillRect(Layout::Rect{bounds.x, bounds.y + 8.0f, 3.0f, bounds.height - 16.0f},
                          tokens.accent, 1.5f);
    }
}

void AudioPlayerWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, CachedLayoutTokens());
}

void AudioPlayerWidget::Layout(const Layout::Rect& bounds, const Theme::Tokens& tokens) {
    CacheLayoutTokens(tokens);
    EnsureChildrenAttached();
    SetBounds(bounds);

    const float padding = tokens.spacing_large;
    const float spacing = tokens.spacing_small;
    const float row_h = 36.0f;
    const float strip_h = 24.0f;
    const float strip_gap = tokens.spacing_small;
    // Transport row sits above the waveform strip, both inside the card.
    const float controls_h = bounds.height - strip_h - strip_gap;
    const float y = bounds.y + std::max(0.0f, (controls_h - row_h) * 0.5f);
    waveform_->Layout(Layout::Rect{
        bounds.x + padding, bounds.y + bounds.height - strip_h - 2.0f,
        bounds.width - 2.0f * padding, strip_h}, CachedLayoutTokens());
    float x = bounds.x + padding;

    play_pause_button_->Layout(Layout::Rect{x, y, 92.0f, row_h}, CachedLayoutTokens());
    x += 92.0f + spacing;
    stop_button_->Layout(Layout::Rect{x, y, 56.0f, row_h}, CachedLayoutTokens());
    x += 56.0f + spacing;
    mute_button_->Layout(Layout::Rect{x, y, 72.0f, row_h}, CachedLayoutTokens());
    x += 72.0f + spacing;

    // Right-side fixed cluster, seek takes whatever remains.
    const float vol_label_w = 34.0f;
    const float vol_slider_w = 84.0f;
    const float fx_w = 52.0f;
    const float status_w = 64.0f;
    const float time_w = 96.0f;
    const float right_fixed =
        vol_label_w + spacing + vol_slider_w + spacing * 2.0f + fx_w + spacing +
        status_w + spacing + time_w;
    const float seek_width = std::max(80.0f,
        bounds.x + bounds.width - padding - x - right_fixed);

    seek_slider_->Layout(Layout::Rect{x, y + 7.0f, seek_width, 22.0f}, CachedLayoutTokens());
    x += seek_width + spacing;
    time_label_->Layout(Layout::Rect{x, y, time_w, row_h}, CachedLayoutTokens());
    x += time_w + spacing;
    volume_label_->Layout(Layout::Rect{x, y + 2.0f, vol_label_w, 32.0f}, CachedLayoutTokens());
    x += vol_label_w + spacing;
    volume_slider_->Layout(Layout::Rect{x, y + 7.0f, vol_slider_w, 22.0f}, CachedLayoutTokens());
    x += vol_slider_w + spacing * 2.0f;
    fx_button_->Layout(Layout::Rect{x, y, fx_w, row_h}, CachedLayoutTokens());
    x += fx_w + spacing;
    status_label_->Layout(Layout::Rect{x, y + 2.0f, status_w, 32.0f}, CachedLayoutTokens());
}

bool AudioPlayerWidget::HandleEvent(const Event&) {
    return false;
}

Layout::Size AudioPlayerWidget::GetMinSize() const {
    return {560.0f, 72.0f};
}

Layout::Size AudioPlayerWidget::GetPreferredSize() const {
    return {720.0f, 84.0f};
}

Layout::Size AudioPlayerWidget::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void AudioPlayerWidget::Tick() {
    SyncPlaybackState();
    waveform_->Tick();
    waveform_->SetPlaying(is_playing_);
}

void AudioPlayerWidget::SyncPlaybackState() {
    is_playing_ = live_state_.load(std::memory_order_relaxed) ==
        static_cast<int>(XER::Content::Audio::PlaybackState::kPlaying);
    UpdateLivePosition(
        live_position_.load(std::memory_order_relaxed),
        live_duration_.load(std::memory_order_relaxed));
}

void AudioPlayerWidget::UpdateLivePosition(double current_time, double duration) {
    current_time_ = current_time;
    duration_ = duration;

    time_label_->SetText(FormatTime(current_time) + " / " + FormatTime(duration));

    if (duration > 0 && !seek_slider_->IsDragging()) {
        syncing_seek_ = true;
        seek_slider_->SetValue(static_cast<float>(current_time / duration));
        syncing_seek_ = false;
    }
    waveform_->SetProgress(duration > 0.0
        ? static_cast<float>(current_time / duration) : 0.0f);
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
    controller_->Seek(value * duration_);
}

void AudioPlayerWidget::OnVolumeChanged(float value) {
    if (controller_) {
        controller_->SetVolume(value);
    }
}

void AudioPlayerWidget::OnMute() {
    if (!controller_) return;
    const bool currently_muted = controller_->IsMuted();
    controller_->Mute(!currently_muted);
    UpdateButtonStates();
}

void AudioPlayerWidget::OnSetVolume(float value) {
    if (!controller_) return;
    controller_->SetVolume(value);
    volume_slider_->SetValue(value);
    UpdateButtonStates();
}

bool AudioPlayerWidget::OnContextMenu(const Event& event) {
    if (event.type != Event::Type::MouseDown ||
        event.mouse_button != MouseButton::Right || !controller_) {
        return false;
    }

    const bool muted = controller_->IsMuted();
    const float volume = controller_->GetVolume();

    std::vector<ContextMenu::Item> items;
    items.push_back({is_playing_ ? "Pause" : "Play",
                     [this]() { OnPlayPause(); }, true, false, false, "Space"});
    items.push_back({"Stop", [this]() { OnStop(); }, true, false, false, ""});
    items.push_back({"", nullptr, false, true});
    items.push_back({muted ? "Unmute" : "Mute", [this]() { OnMute(); },
                     true, false, muted, "M"});
    items.push_back({"", nullptr, false, true});
    for (const float preset : {1.0f, 0.75f, 0.5f, 0.25f}) {
        items.push_back({"Volume " + std::to_string(static_cast<int>(preset * 100)) + "%",
                         [this, preset]() { OnSetVolume(preset); },
                         true, false, std::abs(volume - preset) < 0.03f, ""});
    }
    items.push_back({"", nullptr, false, true});
    items.push_back({"Open FX Rack",
                     [this]() { if (on_open_fx_) on_open_fx_(); },
                     on_open_fx_ != nullptr, false, false, ""});
    items.push_back({"Open Video Preview",
                     [this]() { if (on_open_video_) on_open_video_(); },
                     on_open_video_ != nullptr, false, false, ""});

    auto menu = ContextMenu::Create(std::move(items));
    menu->SetAnchor(event.position);
    ShowOverlay(menu);
    return true;
}

void AudioPlayerWidget::UpdateButtonStates() {
    if (!controller_) return;
    play_pause_button_->SetText(is_playing_ ? "Pause" : "Play");
    mute_button_->SetText(controller_->IsMuted() ? "Unmute" : "Mute");
    const auto state = live_state_.load(std::memory_order_relaxed);
    if (!controller_->IsLoaded()) {
        status_label_->SetText("Ready");
    } else {
        switch (static_cast<XER::Content::Audio::PlaybackState>(state)) {
            case XER::Content::Audio::PlaybackState::kPlaying:
                status_label_->SetText("Playing");
                break;
            case XER::Content::Audio::PlaybackState::kPaused:
                status_label_->SetText("Paused");
                break;
            case XER::Content::Audio::PlaybackState::kError:
                status_label_->SetText("Error");
                break;
            default:
                status_label_->SetText("Stopped");
                break;
        }
    }
}

} // namespace XER::UI::Widgets
