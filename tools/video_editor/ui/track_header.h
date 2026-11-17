// Self-contained track-header widget for the XER video editor timeline.
// Shows the track index badge (V1/A2), the name, the M/H/L toggle chips
// (muted / hidden / locked) and a gain slider. Pure presentation — the
// widget stores a copy of the Track it was given and reports edits
// through the hooks; the host mutates the model and pushes the result
// back via SetTrack().
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "layout/geometry.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Slider;
}

namespace XER::VideoEditor::Ui {

class TrackHeader final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Chip toggles. `which` is 'm' (Track::muted), 'h'
        // (Track::hidden) or 'l' (Track::locked); `on` is the desired
        // new state. The widget does NOT flip its stored flag itself —
        // the host updates the model and pushes SetTrack() back.
        std::function<void(char which, bool on)> on_toggle;
        // Gain slider, dB — maps to Track::gain_db. Fires on every
        // slider change (drag included).
        std::function<void(float db)> on_gain;
    };

    explicit TrackHeader(Hooks hooks = {});

    // Copies the fields the header displays. `index` is the track's
    // position in Project::tracks (drives the V1/A2 badge).
    void SetTrack(const Track& track, int index);
    int TrackIndex() const { return index_; }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {96.0f, 44.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {148.0f, 58.0f};   // matches the timeline header column
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 58.0f};
    }

private:
    void EnsureChildren();
    // Chip rects, right-aligned on the name row. `chip` is 0=M,1=H,2=L.
    XER::UI::Layout::Rect ChipRect(int chip) const;
    XER::UI::Layout::Rect BadgeRect() const;
    // -1 when no chip contains `position`.
    int ChipAt(const XER::UI::Layout::Point& position) const;

    Hooks hooks_;
    bool children_attached_ = false;
    // Held while SetTrack() writes the slider so the programmatic
    // update does not echo back through on_gain.
    bool syncing_gain_ = false;

    int index_ = 0;
    TrackKind kind_ = TrackKind::Video;
    std::string name_;
    bool muted_ = false;
    bool hidden_ = false;
    bool locked_ = false;
    float gain_db_ = 0.0f;

    std::shared_ptr<XER::UI::Widgets::Slider> gain_slider_;
};

}  // namespace XER::VideoEditor::Ui
