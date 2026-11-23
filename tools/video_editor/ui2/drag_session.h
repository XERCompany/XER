// BinDragSession — the shell-owned media-bin→timeline drag handoff.
//
// The widget kit has no global drag-and-drop service: BinGridSurface
// arms a drag on a card press and reports begin/update/end through
// plain callbacks (spec §6 drag handoff). The shell keeps the session
// state HERE (dragged paths, hover affordance on the timeline
// surface) so the surface stays a dumb emitter — the same role the
// legacy app's selected_media_path_ + OnBinDragHover/OnBinMediaDrop
// pair played.
//
// Usage (VeApp):
//   session_.Begin(paths);
//   session_.Update(point, timeline_surface);   // hover feedback
//   if (auto drop = session_.End(point, timeline_surface)) {
//       AddMediaToTimelineAt(path, drop->time, drop->track);  // history
//   }
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "layout/geometry.h"

namespace XER::VideoEditor::Ui2 {

class TimelineSurface;

class BinDragSession {
public:
    // A completed timeline drop — TimeAtDropPoint/TrackAtDropPoint
    // resolved against the drop position.
    struct Drop {
        double time = 0.0;
        int track = -1;
    };

    void Begin(std::vector<std::string> paths) {
        paths_ = std::move(paths);
        active_ = !paths_.empty();
        hovering_ = false;
    }

    // Moves the hover affordance: SetExternalDropHover(true) while the
    // cursor is over the timeline surface's bounds, false elsewhere.
    void Update(XER::UI::Layout::Point point, TimelineSurface* surface);

    // Ends the session: clears the hover and returns the resolved
    // drop target when released inside the timeline bounds, nullptr
    // otherwise (the shell performs the history-backed insert).
    std::optional<Drop> End(XER::UI::Layout::Point point,
                            TimelineSurface* surface);

    // Aborts without a drop (Escape, focus loss) — clears hover.
    void Cancel(TimelineSurface* surface);

    bool Active() const { return active_; }
    const std::vector<std::string>& Paths() const { return paths_; }

private:
    bool active_ = false;
    bool hovering_ = false;
    std::vector<std::string> paths_;
};

}  // namespace XER::VideoEditor::Ui2
