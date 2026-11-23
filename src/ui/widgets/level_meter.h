#ifndef XER_UI_WIDGETS_LEVEL_METER_H
#define XER_UI_WIDGETS_LEVEL_METER_H

#include <functional>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// audiocn-style LevelMeter (audiocn.dev/docs/components/level-meter):
// layered peak + RMS bar meter with ballistics, a dB taper, colored
// zones (low/optimum/high/clip segments mapped to theme tokens, never
// hardcoded colors), a peak-hold tick, and a sticky clip indicator that
// latches at 0 dBFS, stays lit for at least ~1.5 s, and clears on click.
//
// Feed modes (three, like the reference):
//   * SetLevels(peak_db, rms_db)            — push, channel 0 (mono feed)
//   * SetLevels(ch, peak_db, rms_db) / SetLevels(vector) — push per channel
//   * SetFrameSource(fn)                    — pull; polled once per Tick so
//                                             an engine can own the 60 Hz
//                                             level loop (audio-ui `source`)
// Ballistics run in Tick() so pushed values and pulled frames get the
// same smoothing; call sites never need to compute decay themselves.
class LevelMeter final : public Widget {
public:
    // Peak: ~15 ms rise, steady linear fall (console PPM behavior).
    // Vu: slow averaged response (~300 ms time constant both ways).
    // Instant: follows the input exactly — no smoothing.
    enum class Ballistics { Peak, Vu, Instant };

    // Linear maps dB evenly along the track; Audio squares the normalized
    // position so the loud end gets most of the track (console/recorder
    // convention — a -6 dB change near the top moves as much as a -24 dB
    // change near the bottom).
    enum class Taper { Linear, Audio };

    enum class Orientation { Vertical, Horizontal };

    // Zone colors are semantic; Render() resolves them against the active
    // Theme::Tokens so meters follow the theme instead of baked-in RGB.
    enum class ZoneRole {
        Low,      // below optimum — tokens.muted
        Optimum,  // target range — tokens.track_audio
        High,     // hot but not clipping — tokens.warning_chip
        Clip      // 0 dBFS region — tokens.danger
    };
    struct Zone {
        float from_db = -60.0f;   // inclusive lower edge of the segment
        float to_db = 0.0f;       // upper edge
        ZoneRole role = ZoneRole::Optimum;
    };

    struct ChannelLevels {
        float peak_db = -120.0f;
        float rms_db = -120.0f;
    };

    // Pull source polled in Tick(); return one ChannelLevels per channel
    // (extra entries are ignored, missing channels decay to the floor).
    using FrameSource = std::function<std::vector<ChannelLevels>()>;

    LevelMeter();

    // ---- Feed ----
    void SetLevels(float peak_db, float rms_db);   // channel 0 shortcut
    void SetLevels(int channel, float peak_db, float rms_db);
    void SetLevels(const std::vector<ChannelLevels>& levels);
    void SetFrameSource(FrameSource source);

    // ---- Model ----
    void SetChannelCount(int count);
    int ChannelCount() const { return channel_count_; }
    void SetBallistics(Ballistics ballistics);
    Ballistics GetBallistics() const { return ballistics_; }
    void SetTaper(Taper taper);
    void SetDbRange(float min_db, float max_db);
    // dBFS at which the clip indicator latches (default 0 dB).
    void SetClipThreshold(float db);
    // Empty vector restores the default 3-zone optimum/high/clip layout.
    void SetZones(std::vector<Zone> zones);
    // Seconds the peak-hold tick stays before decaying; <= 0 disables it.
    void SetPeakHold(float seconds);
    void SetOrientation(Orientation orientation);

    // Fires once per clip latch (channel index), not every clipped frame.
    void SetOnClip(std::function<void(int channel)> callback);
    bool IsClipped() const;
    void ClearClip();

    // ---- Widget ----
    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    struct ChannelState {
        float peak_db = -120.0f;   // raw pushed/pulled input
        float rms_db = -120.0f;
        float show_peak_db = -120.0f; // after ballistics
        float show_rms_db = -120.0f;
        float hold_db = -120.0f;      // peak-hold tick position
        float hold_time_s = 0.0f;     // countdown before the hold decays
        bool clipped = false;         // sticky latch — cleared by click
        float clip_time_s = 0.0f;     // "fresh clip" timer (bright phase)
    };

    void FeedChannel(int channel, float peak_db, float rms_db);
    float DbToFraction(float db) const;
    Theme::Color ZoneColor(ZoneRole role, const Theme::Tokens& tokens) const;
    void RenderChannel(Rendering::Renderer& renderer,
                       const Theme::Tokens& tokens,
                       int channel, const Layout::Rect& lane,
                       const Layout::Rect& clip_box,
                       const std::vector<Zone>& zones);

    int channel_count_ = 2;
    Ballistics ballistics_ = Ballistics::Peak;
    Taper taper_ = Taper::Audio;
    Orientation orientation_ = Orientation::Vertical;
    float min_db_ = -60.0f;
    float max_db_ = 0.0f;
    float clip_db_ = 0.0f;
    float peak_hold_s_ = 2.0f;
    std::vector<Zone> zones_;
    std::vector<ChannelState> channels_;
    FrameSource frame_source_;
    std::function<void(int)> on_clip_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_LEVEL_METER_H
