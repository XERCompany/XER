// Implementation of ai/object_track.h — the track-object scan
// reduction plus pin/mask geometry. The scan itself is the shared
// ScanClipDetections core (ai/smart_reframe.cc); this file only owns
// the per-probe pick policy, the smoothed-box interpolation and the
// track -> keyframe baking math.
#include "ai/object_track.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace XER::VideoEditor::Ai {
namespace {

std::string LowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Class-filtered pick: the highest-confidence detection whose label
// matches `filter` (case-insensitive; a bare "class_<id>" label also
// matches a numeric filter — the no-.names-file spellings). nullptr
// when the class is absent at this probe.
const TrackDetection* PickClass(const std::vector<TrackDetection>& dets,
                                const std::string& filter) {
    const std::string want = LowerAscii(filter);
    const TrackDetection* best = nullptr;
    for (const TrackDetection& d : dets) {
        const std::string low = LowerAscii(d.label);
        // Numeric filters double as generic class_<id> spellings.
        if (low != want && low != "class_" + want) continue;
        if (best == nullptr || d.confidence > best->confidence) {
            best = &d;
        }
    }
    return best;
}

}  // namespace

bool ScanClipTrack(const Clip& clip, const std::string& model_path,
                   const std::string& labels_path,
                   const ReframeScanParams& params,
                   const std::string& label_filter,
                   const std::function<void(float)>& progress,
                   const std::function<bool()>& should_cancel,
                   ClipTrack* out, std::string* error_out) {
    if (out == nullptr) return false;
    *out = ClipTrack{};
    std::map<std::string, int> seen;      // every label observed
    std::map<std::string, int> picked;    // labels the track used
    ClipReframe samples;
    const bool filtered = !label_filter.empty();
    const bool ok = ScanClipDetections(
        clip, model_path, labels_path, params,
        [&](const std::vector<TrackDetection>& dets)
            -> const TrackDetection* {
            for (const TrackDetection& d : dets) {
                if (!d.label.empty()) {
                    ++seen[LowerAscii(d.label)];
                }
            }
            const TrackDetection* d =
                filtered ? PickClass(dets, label_filter) : nullptr;
            if (d == nullptr) {
                d = PickPrimarySubject(dets);
            }
            if (d != nullptr && !d->label.empty()) {
                ++picked[LowerAscii(d->label)];
            }
            return d;
        },
        progress, should_cancel, &samples, error_out);
    if (!ok) {
        return false;
    }
    out->samples = std::move(samples.samples);
    // Dominant picked class — the name pin menus/inspector show.
    int best_n = 0;
    for (const auto& [label, n] : picked) {
        if (n > best_n) {
            best_n = n;
            out->label = label;
        }
    }
    if (out->label.empty() && filtered) {
        out->label = LowerAscii(label_filter);
    }
    out->classes.reserve(seen.size());
    for (const auto& [label, n] : seen) {
        out->classes.push_back(label);
    }
    return true;
}

TrackBox TrackBoxAt(const ClipTrack& track, double t, double smoothing) {
    TrackBox box;
    const std::vector<ReframeSample>& s = track.samples;
    if (s.empty()) {
        return box;
    }
    const TrackPoint c =
        TrackCenterAt(s, t, std::clamp(smoothing, 0.0, 1.0));
    box.cx = c.x;
    box.cy = c.y;
    // Size rides raw-sample lerp (no velocity cap — a box shrinking
    // as the subject turns away should not lag a full smoothing
    // horizon behind the position it wraps).
    if (t <= s.front().t) {
        box.w = s.front().w;
        box.h = s.front().h;
        return box;
    }
    if (t >= s.back().t) {
        box.w = s.back().w;
        box.h = s.back().h;
        return box;
    }
    const auto next = std::lower_bound(
        s.begin(), s.end(), t,
        [](const ReframeSample& a, double v) { return a.t < v; });
    const ReframeSample& b = *next;
    const ReframeSample& a = *(next - 1);
    const double span = b.t - a.t;
    const double w = span > 1e-9
                         ? std::clamp((t - a.t) / span, 0.0, 1.0)
                         : 1.0;
    box.w = a.w + w * (b.w - a.w);
    box.h = a.h + w * (b.h - a.h);
    return box;
}

bool BakePinKeys(const Clip& tracked, PinAnchor anchor,
                 const PinGeometry& geom, double smoothing,
                 PinKeys* out, std::string* error_out) {
    const auto fail = [&](const std::string& why) {
        if (error_out != nullptr) *error_out = why;
        return false;
    };
    if (out == nullptr) {
        return fail("null out");
    }
    out->pos_x.clear();
    out->pos_y.clear();
    if (tracked.object_track.samples.empty()) {
        return fail("clip has no object track");
    }
    if (geom.src_w <= 0 || geom.src_h <= 0 || geom.canvas_w <= 0 ||
        geom.canvas_h <= 0 || geom.pin_w <= 0.0 ||
        geom.pin_h <= 0.0 || !(geom.pin_scale > 0.0)) {
        return fail("pin geometry is degenerate");
    }
    const double rate =
        tracked.playback_rate > 0.0 ? tracked.playback_rate : 1.0;
    // The pinned frame's anchor point, as a fraction of its own frame.
    double afx = 0.5, afy = 0.0;
    switch (anchor) {
        case PinAnchor::TopCenter:    afx = 0.5; afy = 1.0; break;
        case PinAnchor::Center:       afx = 0.5; afy = 0.5; break;
        case PinAnchor::BottomCenter: afx = 0.5; afy = 0.0; break;
    }
    constexpr double kTimeEps = 1e-4;
    for (const ReframeSample& s : tracked.object_track.samples) {
        const double u = (s.t - tracked.in_point) / rate;
        if (u < 0.0 || u > tracked.duration) {
            continue;   // sample outside the consumed timeline span
        }
        const TrackPoint c = TrackCenterAt(tracked.object_track.samples,
                                           s.t, smoothing);
        // Tracked-box anchor point in SOURCE-frame fractions.
        double fx = c.x;
        double fy = c.y;
        switch (anchor) {
            case PinAnchor::TopCenter:    fy = c.y - s.h * 0.5; break;
            case PinAnchor::Center:       break;
            case PinAnchor::BottomCenter: fy = c.y + s.h * 0.5; break;
        }
        // Source fraction -> canvas fraction through the tracked
        // clip's effective transform at this local time: canvas px =
        // offset*canvas + src_px*scale, so canvas_fraction =
        // offset + frac*src_dim*scale/canvas_dim.
        const Transform eff = EffectiveTransformAt(tracked, u);
        double ax =
            static_cast<double>(eff.offset_x) +
            fx * geom.src_w * static_cast<double>(eff.scale) /
                geom.canvas_w;
        double ay =
            static_cast<double>(eff.offset_y) +
            fy * geom.src_h * static_cast<double>(eff.scale) /
                geom.canvas_h;
        if (anchor == PinAnchor::TopCenter) {
            ay -= geom.margin;
        } else if (anchor == PinAnchor::BottomCenter) {
            ay += geom.margin;
        }
        // Pinned top-left: anchor_canvas - anchor_pin_px*scale/canvas.
        const double ox =
            ax - afx * geom.pin_w * geom.pin_scale / geom.canvas_w;
        const double oy =
            ay - afy * geom.pin_h * geom.pin_scale / geom.canvas_h;
        // Duplicate times (a denser future scan) keep the first key.
        if (!out->pos_x.empty() &&
            std::fabs(out->pos_x.back().time - u) < kTimeEps) {
            continue;
        }
        out->pos_x.push_back(
            {u, static_cast<float>(ox), KeyEase::Linear});
        out->pos_y.push_back(
            {u, static_cast<float>(oy), KeyEase::Linear});
    }
    if (out->pos_x.empty()) {
        return fail("no track sample lands inside the clip's span");
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
