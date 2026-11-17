#include "edit_tool.h"

#include <cmath>
#include <utility>

#include "hand_tool.h"
#include "rate_stretch_tool.h"
#include "razor_tool.h"
#include "ripple_edit_tool.h"
#include "rolling_edit_tool.h"
#include "selection_tool.h"
#include "slide_tool.h"
#include "slip_tool.h"
#include "zoom_tool.h"

namespace XER::VideoEditor::Ui {
namespace {
constexpr double kAdjacencyEpsilon = 1e-4;  // matches model kEpsilon scale
}

bool ClipTrackLocked(const Project& project, const Clip& clip) {
    const Track* track = FindTrack(project, clip.track);
    return track != nullptr && track->locked;
}

std::vector<const Clip*> LinkedClips(const Project& project,
                                     const Clip& clip) {
    std::vector<const Clip*> out{&clip};
    if (clip.link_group.empty()) return out;
    for (const Clip& other : project.clips) {
        if (other.id != clip.id && other.link_group == clip.link_group) {
            out.push_back(&other);
        }
    }
    return out;
}

const Clip* NextClipOnTrack(const Project& project, int track,
                            double after_seconds) {
    const Clip* next = nullptr;
    for (const Clip& clip : project.clips) {
        if (clip.track != track) continue;
        if (clip.timeline_start >= after_seconds - kAdjacencyEpsilon &&
            (next == nullptr ||
             clip.timeline_start < next->timeline_start)) {
            next = &clip;
        }
    }
    return next;
}

const Clip* PrevClipOnTrack(const Project& project, int track,
                            double before_seconds) {
    const Clip* prev = nullptr;
    for (const Clip& clip : project.clips) {
        if (clip.track != track) continue;
        if (ClipEndSeconds(clip) <= before_seconds + kAdjacencyEpsilon &&
            (prev == nullptr ||
             ClipEndSeconds(clip) > ClipEndSeconds(*prev))) {
            prev = &clip;
        }
    }
    return prev;
}

std::unique_ptr<EditTool> MakeEditTool(ToolId id) {
    switch (id) {
        case ToolId::Selection:   return std::make_unique<SelectionTool>();
        case ToolId::Razor:       return std::make_unique<RazorTool>();
        case ToolId::RippleEdit:  return std::make_unique<RippleEditTool>();
        case ToolId::RollingEdit: return std::make_unique<RollingEditTool>();
        case ToolId::Slip:        return std::make_unique<SlipTool>();
        case ToolId::Slide:       return std::make_unique<SlideTool>();
        case ToolId::RateStretch: return std::make_unique<RateStretchTool>();
        case ToolId::Hand:        return std::make_unique<HandTool>();
        case ToolId::Zoom:        return std::make_unique<ZoomTool>();
    }
    return std::make_unique<SelectionTool>();
}

}  // namespace XER::VideoEditor::Ui
