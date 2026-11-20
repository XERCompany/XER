// source_edit.cc — see source_edit.h for the contract. Pure value
// plumbing over the timeline model's own transactional ops.
#include "source_edit.h"

#include <algorithm>
#include <cmath>

namespace XER::VideoEditor {

bool ResolveSourceRange(const SourceMarks& marks, double media_duration,
                        double* in_out, double* out_out,
                        double min_duration) {
    if (in_out == nullptr || out_out == nullptr) {
        return false;
    }
    const double duration =
        std::isfinite(media_duration) ? std::max(0.0, media_duration)
                                      : 0.0;
    double in = marks.HasIn() ? marks.in : 0.0;
    double out = marks.HasOut() ? marks.out : -1.0;
    if (!std::isfinite(in)) in = 0.0;
    if (out >= 0.0 && !std::isfinite(out)) out = -1.0;
    if (duration > 0.0) {
        in = std::clamp(in, 0.0, duration);
        // An out mark may sit past the probed duration — clamp like
        // Premiere instead of failing the whole edit.
        if (out < 0.0) {
            out = duration;
        } else {
            out = std::clamp(out, 0.0, duration);
        }
    } else if (out < 0.0) {
        out = in + kSourceFallbackSeconds;
    }
    in = std::max(0.0, in);
    if (out - in < min_duration) {
        return false;
    }
    *in_out = in;
    *out_out = out;
    return true;
}

Clip MakeSourceClip(const std::string& path, int track,
                    double timeline_start, double src_in, double src_out,
                    int src_w, int src_h, int canvas_w, int canvas_h) {
    Clip clip;
    clip.source_path = path;
    clip.track = track;
    clip.timeline_start = std::max(0.0, timeline_start);
    clip.in_point = std::max(0.0, src_in);
    clip.duration = std::max(0.0, src_out - src_in);
    clip.playback_rate = 1.0;
    if (src_w > 0 && src_h > 0 && canvas_w > 0 && canvas_h > 0) {
        clip.transform =
            FitTransformToCanvas(src_w, src_h, canvas_w, canvas_h);
    }
    return clip;
}

bool SourceEditAt(Project& project, const Clip& clip,
                  const MediaBounds& bounds, bool insert,
                  std::string* error_out) {
    return insert ? InsertClipAt(project, clip, bounds, error_out)
                  : OverwriteClipAt(project, clip, bounds, error_out);
}

}  // namespace XER::VideoEditor
