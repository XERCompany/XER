// Source monitor for the XER video editor — Premiere-style clip
// preview surface next to the program monitor. Unlike MonitorWidget
// it owns NO decode: the app pumps frames from a dedicated
// PreviewEngine (a synthetic single-clip Project on the media file)
// through SetFrame(), so every visible pixel is real decoded video,
// and seeks/steps route back through the engine via the hooks below.
//
//   * seek bar (mini ruler): marked in/out brackets + playhead;
//     click/drag seeks
//   * transport row: play/pause, |◀ ▶| frame step, { IN, OUT } mark
//     buttons, INS (,) / OVR (.) three-point-edit buttons
//   * footer: source timecode + the marked range (or the media range
//     when nothing is marked)
//   * header: "SOURCE" + the loaded clip's basename
//
// All editing semantics (marks map, insert/overwrite ops, shuttle)
// live in the app — the widget is a dumb surface that reports
// gestures through Hooks and mirrors state set by the app.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
}

namespace XER::VideoEditor::Ui {

class SourceMonitorWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired on any press inside the monitor — the host uses it to
        // route keyboard transport (Space/JKL/arrows/I/O) at the
        // source monitor instead of the program monitor.
        std::function<void()> on_activate;
        std::function<void(double)> on_seek_request;   // source seconds
        std::function<void()> on_play_toggle;
        std::function<void(int)> on_frame_step;        // dir = -1|+1
        std::function<void()> on_mark_in;              // set mark at t
        std::function<void()> on_mark_out;
        std::function<void()> on_insert;               // , edit
        std::function<void()> on_overwrite;            // . edit
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit SourceMonitorWidget(Hooks hooks = {});
    ~SourceMonitorWidget() override = default;

    // Loads a media file for preview. `duration` is source seconds
    // (<=0 = unknown), fps_num/fps_den drive the timecode display and
    // the +/-frame step. Clears the frame, marks and playhead — the
    // host re-applies persisted marks via SetMarks() right after.
    void SetSource(const std::string& path, const std::string& name,
                   double duration, int fps_num, int fps_den,
                   bool has_video);
    void ClearSource();
    bool HasSource() const { return !path_.empty(); }
    const std::string& SourcePath() const { return path_; }
    double Duration() const { return duration_; }
    double Time() const { return time_; }
    bool HasVideo() const { return has_video_; }

    // Latest decoded frame, packed BGRA8 (copied). Empty/0x0 clears.
    void SetFrame(const std::vector<std::uint8_t>& bgra, int width,
                  int height);
    // Playhead readout only — engine seeks produce frames on their own.
    void SetTime(double seconds);
    // App-reported transport state for the play-button glyph.
    void SetPlaying(bool playing);
    bool IsPlaying() const { return playing_; }
    // Marked range in source seconds; < 0 = unmarked (both sides
    // independent, matching Premiere's separate in/out marks).
    void SetMarks(double in_seconds, double out_seconds);
    std::pair<double, double> Marks() const {
        return {mark_in_, mark_out_};
    }
    // One-line status shown in place of the video when no frame is
    // available yet ("Decoding…", audio-only label, errors).
    void SetStatusText(const std::string& text, bool is_error = false);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {280.0f, 240.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {560.0f, 440.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    // Transport row indexes — the buttons themselves are
    // Widgets::Button children; the enum only feeds ButtonRect().
    enum class Button {
        Play,
        PrevFrame,
        NextFrame,
        MarkIn,
        MarkOut,
        Insert,
        Overwrite,
    };

    // Lazy child setup: transport buttons + the header/footer labels.
    void EnsureChildren();
    // Mirrors playing/marks/enabled state onto the child buttons and
    // refreshes the header/footer label text — called every Render.
    void SyncTransportChildren();

    // Geometry — pure functions of GetBounds().
    XER::UI::Layout::Rect VideoAreaRect() const;
    XER::UI::Layout::Rect RulerRect() const;
    XER::UI::Layout::Rect TransportRect() const;
    XER::UI::Layout::Rect FooterRect() const;
    XER::UI::Layout::Rect ButtonRect(Button button) const;
    XER::UI::Layout::Rect FrameDestRect() const;
    double RulerTimeAtX(float x) const;
    void EmitSeek(double t);
    // Footer readout text ("TC — TC · Ns marked" / hint line).
    std::string FooterText() const;

    void DrawRuler(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens,
                   const XER::UI::Layout::Rect& ruler);
    // The transport row's drawn remainder — the "current / duration"
    // readout; the buttons themselves are child widgets now.
    void DrawTransport(XER::UI::Rendering::Renderer& renderer,
                       const XER::UI::Theme::Tokens& tokens,
                       const XER::UI::Layout::Rect& row);

    Hooks hooks_;
    std::string path_;
    std::string name_;
    double duration_ = 0.0;
    int fps_num_ = 30;
    int fps_den_ = 1;
    bool has_video_ = true;
    double time_ = 0.0;
    bool playing_ = false;
    double mark_in_ = -1.0;
    double mark_out_ = -1.0;
    bool ruler_drag_ = false;

    std::vector<std::uint8_t> frame_;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::string status_text_ = "No clip loaded";
    bool status_error_ = false;

    // Transport row children (indexed by the Button enum) plus the
    // header/footer labels.
    bool transport_children_ = false;
    std::shared_ptr<XER::UI::Widgets::Button> play_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> prev_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> next_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> mark_in_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> mark_out_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> insert_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> overwrite_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> header_label_;
    std::shared_ptr<XER::UI::Widgets::Label> footer_label_;
};

}  // namespace XER::VideoEditor::Ui
