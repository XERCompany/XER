#include "video_preview_widget.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#include "helper/string_utils.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/slider.h"
#include "widgets/switch.h"
#include "widgets/text_input.h"

namespace XER::UI::Widgets {

namespace {

#ifdef _WIN32
std::wstring PickVideoFile(HWND owner) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter =
        L"Videos\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.m4v;*.ts\0"
        L"All files\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"mp4";
    ofn.lpstrTitle = L"Open video";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return std::wstring(file);
}
#endif

std::string FormatTime(double seconds) {
    if (seconds < 0.0 || seconds != seconds) return "--:--";
    const int total = static_cast<int>(seconds);
    const int m = total / 60;
    const int s = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

} // namespace

VideoPreviewWidget::VideoPreviewWidget(void* parent_hwnd)
    : parent_hwnd_(parent_hwnd) {
    engine_ = std::make_unique<XER::Content::Video::VideoEngine>();
}

VideoPreviewWidget::~VideoPreviewWidget() = default;

void VideoPreviewWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    path_input_ = std::make_shared<TextInput>("");
    path_input_->SetPlaceholder("Video file path…");
    path_input_->SetTooltip("Path to a video file (mp4, mkv, avi, …)");

    browse_btn_ = std::make_shared<Button>("Browse…", ButtonStyle::Outline);
    browse_btn_->SetTooltip("Choose a video file");
    browse_btn_->SetOnClick([this] { OnBrowse(); });

    load_btn_ = std::make_shared<Button>("Load", ButtonStyle::Primary);
    load_btn_->SetTooltip("Decode and preview the video");
    load_btn_->SetOnClick([this] { OnLoad(); });

    play_btn_ = std::make_shared<Button>("Play", ButtonStyle::Secondary);
    play_btn_->SetTooltip("Play / pause (Space)");
    play_btn_->SetOnClick([this] { OnTogglePlay(); });

    stop_btn_ = std::make_shared<Button>("Stop", ButtonStyle::Ghost);
    stop_btn_->SetTooltip("Stop and rewind to the start");
    stop_btn_->SetOnClick([this] {
        engine_->Stop();
        RefreshTransportUi();
    });

    seek_slider_ = std::make_shared<Slider>(0.0f, 1.0f, 0.0f);
    seek_slider_->SetTooltip("Seek — drag to scrub");
    seek_slider_->SetOnValueChanged([this](float v) {
        if (syncing_seek_) return;
        const double dur = engine_->GetDuration();
        if (dur > 0.0) {
            engine_->Seek(static_cast<double>(v) * dur);
        }
    });

    time_label_ = std::make_shared<Label>("0:00 / 0:00", true);

    status_label_ = std::make_shared<Label>("No video loaded", true);

    loop_switch_ = std::make_shared<Switch>(false);
    loop_switch_->SetTooltip("Loop playback");
    loop_switch_->SetOnChanged([this](bool on) {
        engine_->SetLoop(on);
    });

    AddChild(path_input_);
    AddChild(browse_btn_);
    AddChild(load_btn_);
    AddChild(play_btn_);
    AddChild(stop_btn_);
    AddChild(seek_slider_);
    AddChild(time_label_);
    AddChild(status_label_);
    AddChild(loop_switch_);
}

void VideoPreviewWidget::SetPreviewActive(bool active) {
    if (preview_active_ == active) return;
    preview_active_ = active;
    if (!active) {
        if (engine_->IsPlaying()) {
            was_playing_before_hide_ = true;
            engine_->Pause();
        }
    } else if (was_playing_before_hide_) {
        was_playing_before_hide_ = false;
        engine_->Play();
    }
}

void VideoPreviewWidget::OnBrowse() {
#ifdef _WIN32
    const std::wstring picked = PickVideoFile(
        static_cast<HWND>(parent_hwnd_));
    if (!picked.empty()) {
        path_input_->SetValue(XER::Engine::Helper::String::WideToUtf8(picked));
    }
#endif
}

void VideoPreviewWidget::OnLoad() {
    const std::string path = path_input_->GetValue();
    if (path.empty()) {
        SetStatus("Enter a video file path", true);
        return;
    }
    // Show the cached poster instantly while the decoder spins up.
    poster_frame_ = engine_->RecallFrame(path);
    if (poster_frame_ == nullptr) {
        poster_frame_ = engine_->GetCachedPoster(path);
    }
    drawn_seq_ = 0;
    std::string error;
    if (!engine_->Open(path, &error)) {
        SetStatus("Open failed — " + error, true);
        return;
    }
    SetStatus("Loading…");
    status_dirty_ = true;
    RefreshTransportUi();
}

void VideoPreviewWidget::OnTogglePlay() {
    if (!engine_->IsOpen()) {
        SetStatus("Load a video first", true);
        return;
    }
    if (engine_->IsPlaying()) {
        engine_->Pause();
    } else {
        engine_->Play();
    }
    RefreshTransportUi();
}

void VideoPreviewWidget::SetStatus(const std::string& text, bool is_error) {
    status_label_->SetText(text);
    (void)is_error;
}

void VideoPreviewWidget::RefreshTransportUi() {
    const bool open = engine_->IsOpen();
    const bool playing = engine_->IsPlaying();
    play_btn_->SetText(playing ? "Pause" : "Play");
    play_btn_->SetEnabled(open);
    stop_btn_->SetEnabled(open);
    seek_slider_->SetEnabled(open && engine_->GetDuration() > 0.0);
    const double dur = engine_->GetDuration();
    if (dur > 0.0) {
        seek_slider_->SetRange(0.0f, 1.0f);
    }
    time_label_->SetText(FormatTime(engine_->GetPosition()) + " / " +
                         FormatTime(dur));
}

void VideoPreviewWidget::Render(Rendering::Renderer& renderer,
                                const Theme::Tokens& tokens) {
    EnsureChildren();
    const auto b = GetBounds();
    renderer.FillRect(b, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(b, tokens.border, 1.0f, tokens.radius_large);

    // Video surface: letterboxed inside the reserved area, on a darker well.
    if (video_area_.width > 4.0f && video_area_.height > 4.0f) {
        Theme::Color well = tokens.background;
        renderer.FillRect(video_area_, well, tokens.radius_medium);
        renderer.StrokeRect(video_area_, tokens.border, 1.0f,
                            tokens.radius_medium);

        const XER::Content::Video::VideoFrame* frame = nullptr;
        if (draw_frame_ != nullptr && !draw_frame_->bgra.empty()) {
            frame = draw_frame_.get();
        } else if (poster_frame_ != nullptr &&
                   !poster_frame_->bgra.empty()) {
            frame = poster_frame_.get();
        }
        if (frame != nullptr) {
            const float fa = frame->width / static_cast<float>(frame->height);
            const float aa = video_area_.width / video_area_.height;
            float dw = video_area_.width;
            float dh = video_area_.height;
            if (fa > aa) {
                dh = video_area_.width / fa;
            } else {
                dw = video_area_.height * fa;
            }
            video_dest_ = Layout::Rect{
                video_area_.x + (video_area_.width - dw) * 0.5f,
                video_area_.y + (video_area_.height - dh) * 0.5f,
                dw, dh};
            have_dest_ = true;
            renderer.DrawFrameBgra(frame->bgra.data(), frame->width,
                                   frame->height, video_dest_, 1.0f);
        } else {
            have_dest_ = false;
            const Rendering::TextStyle hint{tokens.muted,
                                            tokens.font_small, 400};
            renderer.DrawText("No video — pick a file above",
                              video_area_, hint);
        }
    }
}

void VideoPreviewWidget::Layout(const Layout::Rect& bounds) {
    Layout(bounds, Theme::Tokens{});
}

void VideoPreviewWidget::Layout(const Layout::Rect& bounds,
                                const Theme::Tokens& tokens) {
    EnsureChildren();
    SetBounds(bounds);

    const float pad = tokens.spacing_medium;
    const float gap = tokens.spacing_small;
    const float row_h = 32.0f;

    float y = bounds.y + pad * 0.6f;

    // Row 1: file picker.
    const float browse_w = 92.0f;
    const float load_w = 64.0f;
    browse_btn_->Layout(Layout::Rect{
        bounds.x + bounds.width - pad - browse_w, y, browse_w, row_h});
    load_btn_->Layout(Layout::Rect{
        bounds.x + bounds.width - pad - browse_w - gap - load_w, y,
        load_w, row_h});
    path_input_->Layout(Layout::Rect{
        bounds.x + pad, y,
        bounds.width - 2.0f * pad - browse_w - load_w - 2.0f * gap,
        row_h});
    y += row_h + gap;

    // Row 2: transport — play/stop, seek slider, time, loop.
    const float tbtn_w = 64.0f;
    play_btn_->Layout(Layout::Rect{bounds.x + pad, y, tbtn_w, row_h});
    stop_btn_->Layout(Layout::Rect{bounds.x + pad + tbtn_w + gap, y,
                                   tbtn_w, row_h});
    const float time_w = 110.0f;
    const float loop_w = 46.0f;
    seek_slider_->Layout(Layout::Rect{
        bounds.x + pad + 2.0f * tbtn_w + 2.0f * gap, y,
        bounds.width - 2.0f * pad - 2.0f * tbtn_w - 2.0f * gap -
            time_w - loop_w - 3.0f * gap,
        row_h});
    time_label_->Layout(Layout::Rect{
        bounds.x + bounds.width - pad - time_w - loop_w - gap, y,
        time_w, row_h});
    loop_switch_->Layout(Layout::Rect{
        bounds.x + bounds.width - pad - loop_w, y, loop_w, row_h});
    y += row_h + gap;

    // Row 3: status line.
    status_label_->Layout(Layout::Rect{bounds.x + pad, y,
                                       bounds.width - 2.0f * pad, 20.0f});
    y += 20.0f + gap;

    // Rest: video surface.
    video_area_ = Layout::Rect{bounds.x + pad, y,
                               bounds.width - 2.0f * pad,
                               std::max(40.0f, bounds.y + bounds.height -
                                                   pad - y)};
}

bool VideoPreviewWidget::HandleEvent(const Event& event) {
    // Click on the video surface toggles play/pause — standard preview UX.
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left && engine_->IsOpen() &&
        have_dest_ && video_dest_.Contains(event.position)) {
        OnTogglePlay();
        return true;
    }
    return false;
}

void VideoPreviewWidget::Tick(double dt_seconds) {
    engine_->Tick();

    // New frame → redraw.
    const auto frame = engine_->GetLatestFrame();
    if (frame != nullptr && frame->sequence != drawn_seq_) {
        drawn_seq_ = frame->sequence;
        draw_frame_ = frame;
        Invalidate();
    }

    // Status line follows engine state/errors.
    if (status_dirty_) {
        status_dirty_ = false;
        status_label_->SetText(engine_->GetStatusText());
    } else {
        const std::string s = engine_->GetStatusText();
        if (s != status_label_->GetText()) {
            status_label_->SetText(s);
        }
    }

    // Transport chrome ~15 Hz — cheap and avoids per-frame layout churn.
    ui_refresh_acc_ += dt_seconds;
    if (ui_refresh_acc_ >= 0.066) {
        ui_refresh_acc_ = 0.0;
        RefreshTransportUi();
        // Seek slider follows playback unless the user is dragging it.
        const double dur = engine_->GetDuration();
        const double pos = engine_->GetPosition();
        if (dur > 0.0 && pos >= 0.0 && !seek_slider_->IsDragging()) {
            syncing_seek_ = true;
            seek_slider_->SetValue(
                static_cast<float>(pos / dur));
            syncing_seek_ = false;
        }
    }
}

} // namespace XER::UI::Widgets
