#include "razor_tool.h"

namespace XER::VideoEditor::Ui {

bool RazorTool::OnPointerDown(const ToolContext& ctx,
                              ToolCommit& commit) {
    if (ctx.clip == nullptr) {
        // Clicks on empty track space / gaps are swallowed — the razor
        // never seeks or selects (that is the Selection tool's job).
        return ctx.track >= 0;
    }
    if (ClipTrackLocked(*ctx.project, *ctx.clip)) {
        commit.SetStatus("track is locked", true);
        return true;
    }
    // The cut snaps like every other edit (markers, clip edges,
    // playhead, work-area edges — CollectSnapPoints).
    const double at = commit.SnapCandidate(ctx.time, ctx.clip->track);
    commit.SplitClip(ctx.clip->id, at);
    return true;
}

}  // namespace XER::VideoEditor::Ui
