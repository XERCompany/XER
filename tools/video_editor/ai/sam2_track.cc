// Implementation of ai/sam2_track.h — per-frame re-prompt SAM2.1
// tracking (the image-mode export's NLE answer: no memory graphs,
// so each probe feeds the previous good mask's bbox + centroid back
// as the prompt). The decode loop mirrors ai/smart_reframe.cc's
// ScanClipFrames cadence (keyframe seek + bounded forward discard)
// but starts at the user's T0 instead of the clip head — the armed
// click lands mid-clip and tracking propagates forward from there.
#include "ai/sam2_track.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "media_decoder.h"

namespace XER::VideoEditor::Ai {
namespace {

// Box-averaged BGRA downscale — same recipe as smart_reframe.cc's
// DownscaleBgra (kept local: the scan there is file-static).
std::vector<std::uint8_t> DownscaleBgra(
    const std::vector<std::uint8_t>& src, int src_w, int src_h,
    int dst_w, int dst_h) {
    std::vector<std::uint8_t> dst(
        static_cast<std::size_t>(dst_w) * dst_h * 4u, 0);
    for (int y = 0; y < dst_h; ++y) {
        const int y0 = y * src_h / dst_h;
        const int y1 = std::max(y0 + 1, (y + 1) * src_h / dst_h);
        for (int x = 0; x < dst_w; ++x) {
            const int x0 = x * src_w / dst_w;
            const int x1 = std::max(x0 + 1, (x + 1) * src_w / dst_w);
            unsigned acc[4] = {0, 0, 0, 0};
            unsigned n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const std::uint8_t* row =
                    src.data() +
                    (static_cast<std::size_t>(sy) * src_w) * 4u;
                for (int sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* p = row + sx * 4;
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                    acc[3] += p[3];
                    ++n;
                }
            }
            std::uint8_t* d =
                dst.data() +
                (static_cast<std::size_t>(y) * dst_w + x) * 4u;
            const unsigned div = n > 0 ? n : 1u;
            d[0] = static_cast<std::uint8_t>(acc[0] / div);
            d[1] = static_cast<std::uint8_t>(acc[1] / div);
            d[2] = static_cast<std::uint8_t>(acc[2] / div);
            d[3] = static_cast<std::uint8_t>(acc[3] / div);
        }
    }
    return dst;
}

// Aspect-derived grid height: cells stay roughly square on the
// probed frame (gw*frame_h/frame_w), clamped to a sane band.
int GridHeightFor(int gw, int frame_w, int frame_h) {
    if (frame_w <= 0 || frame_h <= 0) return gw;
    const int gh = static_cast<int>(std::lround(
        static_cast<double>(gw) * frame_h / frame_w));
    return std::clamp(gh, 8, 128);
}

// Previous-mask state, all in FRAME FRACTIONS so probe-resolution
// changes never skew the re-prompt.
struct PrevMask {
    bool valid = false;
    float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;   // bbox fractions
    float cx = 0.5f, cy = 0.5f;                 // centroid fractions
    std::vector<std::uint8_t> bits;             // grid bitmap
};

void SetPrevFromMask(const Sam2Mask& mask, int gw, int gh,
                     PrevMask* prev) {
    float cx = 0, cy = 0, w = 0, h = 0;
    if (!Sam2MaskBounds(mask.logits.data(), mask.w, mask.h, &cx, &cy,
                        &w, &h)) {
        prev->valid = false;
        return;
    }
    prev->bx0 = cx - w * 0.5f;
    prev->by0 = cy - h * 0.5f;
    prev->bx1 = cx + w * 0.5f;
    prev->by1 = cy + h * 0.5f;
    float ccx = 0, ccy = 0;
    if (Sam2MaskCentroid(mask.logits.data(), mask.w, mask.h, &ccx,
                         &ccy)) {
        prev->cx = ccx;
        prev->cy = ccy;
    } else {
        prev->cx = cx;
        prev->cy = cy;
    }
    prev->bits = Sam2MaskBits(mask.logits.data(), mask.w, mask.h, gw,
                              gh);
    prev->valid = true;
}

// Builds the re-prompt for probe dims (pw x ph): the previous good
// mask's bbox (grown `margin` of its size, clamped to frame) as the
// box prompt PLUS its centroid as a foreground point — the box
// alone under-guides deformations, the centroid alone drifts off
// partial masks; both is still one prompt group to the decoder.
Sam2Prompt RepromptFromPrev(const PrevMask& prev, int pw, int ph,
                            float margin) {
    Sam2Prompt p;
    const float bw = prev.bx1 - prev.bx0;
    const float bh = prev.by1 - prev.by0;
    const float x0 = std::clamp(prev.bx0 - bw * margin, 0.0f, 1.0f);
    const float y0 = std::clamp(prev.by0 - bh * margin, 0.0f, 1.0f);
    const float x1 = std::clamp(prev.bx1 + bw * margin, 0.0f, 1.0f);
    const float y1 = std::clamp(prev.by1 + bh * margin, 0.0f, 1.0f);
    p.has_box = true;
    p.box[0] = x0 * pw;
    p.box[1] = y0 * ph;
    p.box[2] = x1 * pw;
    p.box[3] = y1 * ph;
    p.points = {prev.cx * pw, prev.cy * ph};
    p.labels = {1};   // foreground
    return p;
}

}  // namespace

bool ScanClipMaskTrack(
    const Clip& clip, const std::string& encoder_path,
    const std::string& decoder_path, const Sam2TrackParams& params,
    const std::function<void(float)>& progress,
    const std::function<bool()>& should_cancel, ClipMaskTrack* out,
    std::string* error_out) {
    if (out == nullptr) return false;
    *out = ClipMaskTrack{};
    if (clip.source_path.empty() || clip.is_adjustment) {
        if (error_out) *error_out = "clip has no media to scan";
        return false;
    }
    const bool has_prompt = !params.prompt_points.empty() ||
                            params.has_box;
    if (!has_prompt) {
        if (error_out) {
            *error_out = "mask track needs a T0 prompt "
                         "(point or box)";
        }
        return false;
    }
    if (encoder_path.empty() || decoder_path.empty()) {
        if (error_out) *error_out = Sam2ModelHint();
        return false;
    }
    Sam2Encoder encoder;
    Sam2Decoder decoder;
    std::string err;
    if (!encoder.Load(encoder_path, &err)) {
        if (error_out) *error_out = err;
        return false;
    }
    if (!decoder.Load(decoder_path, &err)) {
        if (error_out) *error_out = err;
        return false;
    }

    MediaDecoderPtr media = MakeBridgeDecoder();
    if (media == nullptr) {
        if (error_out) *error_out = "no media decoder";
        return false;
    }
    if (!media->Open(clip.source_path, &err)) {
        if (error_out) *error_out = "decode open: " + err;
        return false;
    }
    if (media->GetWidth() <= 0 || media->GetHeight() <= 0) {
        if (error_out) *error_out = "media has no video stream";
        return false;
    }

    const double rate =
        clip.playback_rate > 0.0 ? clip.playback_rate : 1.0;
    const double span_src = clip.duration * rate;
    const double clip_begin = std::max(0.0, clip.in_point);
    const double t_end =
        clip_begin + std::min(span_src, params.scan.max_seconds);
    // T0 = the clicked source time, clamped inside the consumed span.
    const double t0 = params.start_seconds >= 0.0
                          ? std::clamp(params.start_seconds,
                                       clip_begin, t_end)
                          : clip_begin;
    const double step = params.scan.sample_step > 0.02
                            ? params.scan.sample_step
                            : 0.5;
    const int probes =
        static_cast<int>(std::floor((t_end - t0) / step + 1e-6)) + 1;
    if (probes <= 0 || t0 >= t_end) {
        if (error_out) *error_out = "no span ahead of the prompt";
        return false;
    }
    const int detect_width = params.scan.detect_width > 0
                                 ? std::min(params.scan.detect_width,
                                            kSam2InputSize)
                                 : kSam2InputSize;

    std::vector<std::uint8_t> bgra;
    std::vector<std::uint8_t> kept;
    std::vector<std::uint8_t> small;
    int width = 0, height = 0;
    PrevMask prev;
    int misses = 0;
    const int gw = params.grid_w > 0 ? params.grid_w : kSam2TrackGridW;
    int gh = 0;
    bool recorded_any = false;

    for (int i = 0; i < probes; ++i) {
        if (should_cancel != nullptr && should_cancel()) {
            if (error_out) *error_out = "cancelled";
            return false;
        }
        const double t = t0 + i * step;
        media->Seek(t);
        constexpr int kMaxForwardFrames = 300;
        kept.clear();
        int kept_w = 0, kept_h = 0;
        for (int f = 0;
             f < kMaxForwardFrames &&
             media->NextFrame(&bgra, &width, &height);
             ++f) {
            bgra.swap(kept);
            kept_w = width;
            kept_h = height;
            if (media->GetPositionSeconds() >= t) break;
        }
        if (progress) {
            progress(static_cast<float>(i + 1) /
                     static_cast<float>(probes));
        }
        if (kept.empty() || kept_w <= 0 || kept_h <= 0) {
            continue;   // undecodable probe — no sample, prev held
        }
        // Inference frame: bounded to detect_width; fractions keep
        // every coordinate resolution-independent.
        int pw = kept_w, ph = kept_h;
        small.clear();
        if (pw > detect_width) {
            pw = detect_width;
            ph = std::max(1, static_cast<int>(std::llround(
                                 kept_h * (static_cast<double>(pw) /
                                           kept_w))));
            small = DownscaleBgra(kept, kept_w, kept_h, pw, ph);
        }
        const std::uint8_t* pixels =
            small.empty() ? kept.data() : small.data();
        const int probe_w = pw, probe_h = ph;
        if (gh <= 0) {
            gh = params.grid_h > 0
                     ? params.grid_h
                     : GridHeightFor(gw, probe_w, probe_h);
            out->grid_w = gw;
            out->grid_h = gh;
        }

        Sam2Embedding emb;
        std::string run_err;
        if (!encoder.Encode(pixels, probe_w, probe_h, probe_w * 4,
                            &emb, &run_err)) {
            continue;   // backend hiccup — no sample, prev held
        }
        // First DECODED probe consumes the user prompt; every later
        // probe re-prompts from the previous good mask.
        Sam2Prompt prompt;
        if (i == 0 || !prev.valid) {
            for (std::size_t pi = 0; pi + 1 < params.prompt_points.size();
                 pi += 2) {
                prompt.points.push_back(
                    params.prompt_points[pi] * probe_w);
                prompt.points.push_back(
                    params.prompt_points[pi + 1] * probe_h);
            }
            prompt.labels.reserve(params.prompt_labels.size());
            for (int l : params.prompt_labels) {
                prompt.labels.push_back(l);
            }
            prompt.has_box = params.has_box;
            for (int b = 0; b < 4; ++b) {
                prompt.box[b] =
                    params.box_norm[b] *
                    (b % 2 == 0 ? probe_w : probe_h);
            }
        } else {
            prompt = RepromptFromPrev(prev, probe_w, probe_h, 0.15f);
        }
        Sam2Mask mask;
        if (!decoder.Decode(emb, prompt, &mask, &run_err)) {
            continue;
        }
        // Drift verdict: the chosen head must clear its own IoU
        // floor AND (after T0) stay glued to the previous mask —
        // a hard cut or occlusion collapses the second check.
        const float mask_iou =
            prev.valid
                ? Sam2BitsIoU(
                      Sam2MaskBits(mask.logits.data(), mask.w, mask.h,
                                   gw, gh)
                          .data(),
                      prev.bits.data(), gw, gh)
                : 1.0f;
        const bool bad =
            mask.iou < params.min_iou_score ||
            (prev.valid && mask_iou < params.min_mask_iou);
        if (bad) {
            ++misses;
            if (prev.valid && misses <= params.max_misses) {
                // Coast sample: freeze the last good mask, flag the
                // boundary so the interpolator snaps (never glides a
                // stale mask onto new content), zero confidence so
                // the gap is auditable.
                ClipMaskSample s;
                s.t = t;
                s.cx = (prev.bx0 + prev.bx1) * 0.5f;
                s.cy = (prev.by0 + prev.by1) * 0.5f;
                s.w = prev.bx1 - prev.bx0;
                s.h = prev.by1 - prev.by0;
                s.confidence = 0.0f;
                s.scene_boundary = true;
                s.bits = prev.bits;
                out->samples.push_back(std::move(s));
                continue;
            }
            break;   // unrecoverable drift — the track stops
        }
        misses = 0;
        ClipMaskSample s;
        s.t = t;
        s.confidence = mask.iou;
        float cx = 0, cy = 0, w = 0, h = 0;
        const bool have_bounds =
            Sam2MaskBounds(mask.logits.data(), mask.w, mask.h, &cx,
                           &cy, &w, &h);
        if (!have_bounds) {
            // Empty mask: treat as a miss on the same drift rules.
            ++misses;
            if (prev.valid && misses <= params.max_misses) {
                ClipMaskSample coast;
                coast.t = t;
                coast.cx = (prev.bx0 + prev.bx1) * 0.5f;
                coast.cy = (prev.by0 + prev.by1) * 0.5f;
                coast.w = prev.bx1 - prev.bx0;
                coast.h = prev.by1 - prev.by0;
                coast.confidence = 0.0f;
                coast.scene_boundary = true;
                coast.bits = prev.bits;
                out->samples.push_back(std::move(coast));
                continue;
            }
            break;
        }
        s.cx = std::clamp(cx, 0.0f, 1.0f);
        s.cy = std::clamp(cy, 0.0f, 1.0f);
        s.w = std::clamp(w, 0.0f, 1.0f);
        s.h = std::clamp(h, 0.0f, 1.0f);
        s.scene_boundary = false;
        s.bits = Sam2MaskBits(mask.logits.data(), mask.w, mask.h, gw,
                              gh);
        SetPrevFromMask(mask, gw, gh, &prev);
        out->samples.push_back(std::move(s));
        recorded_any = true;
    }
    if (!recorded_any && out->samples.empty()) {
        if (error_out) {
            *error_out = "sam2 produced no usable mask — the first "
                         "prompt may have missed the subject";
        }
        return false;
    }
    out->label = "sam";
    return true;
}

// ---- render-side mask resolution -----------------------------------

namespace {

// Unpacks one packed grid bitmap to A8 (0/255 per cell); a null
// bitmap unpacks as full-zero coverage.
void UnpackBits(const std::uint8_t* bits, int gw, int gh,
                std::vector<std::uint8_t>* a8) {
    a8->assign(static_cast<std::size_t>(gw) * gh, 0);
    if (bits == nullptr) return;
    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            if (Sam2BitAt(bits, gw, gh, x, y)) {
                (*a8)[static_cast<std::size_t>(y) * gw + x] = 255;
            }
        }
    }
}

// Nearest-bracket index pair for `t` in the sorted sample stream.
// out {i0, i1, w01} — w01 is the lerp weight toward i1.
void BracketSamples(const std::vector<ClipMaskSample>& s, double t,
                    std::size_t* i0, std::size_t* i1,
                    double* w01) {
    if (t <= s.front().t) {
        *i0 = *i1 = 0;
        *w01 = 0.0;
        return;
    }
    if (t >= s.back().t) {
        *i0 = *i1 = s.size() - 1;
        *w01 = 0.0;
        return;
    }
    // Small tracks — linear scan is fine.
    for (std::size_t i = 1; i < s.size(); ++i) {
        if (s[i].t >= t) {
            *i0 = i - 1;
            *i1 = i;
            const double span = s[i].t - s[i - 1].t;
            *w01 = span > 1e-9 ? (t - s[i - 1].t) / span : 1.0;
            return;
        }
    }
    *i0 = *i1 = s.size() - 1;
    *w01 = 0.0;
}

}  // namespace

bool MaskTrackAt(const ClipMaskTrack& track, double t,
                 std::vector<std::uint8_t>* a8_out,
                 TrackBox* box_out) {
    const int gw = track.grid_w;
    const int gh = track.grid_h;
    if (a8_out == nullptr || gw <= 0 || gh <= 0 ||
        track.samples.empty()) {
        if (a8_out != nullptr) a8_out->clear();
        if (box_out != nullptr) *box_out = TrackBox{};
        return false;
    }
    std::size_t i0 = 0, i1 = 0;
    double w01 = 0.0;
    BracketSamples(track.samples, t, &i0, &i1, &w01);
    const ClipMaskSample& a = track.samples[i0];
    const ClipMaskSample& b = track.samples[i1];
    // A boundary-flagged later sample snaps: the earlier mask holds
    // until b.t exactly (never crossfade a stale mask across a cut).
    const bool snap = b.scene_boundary && i0 != i1;
    const ClipMaskSample& from = snap ? a : (w01 >= 0.5 ? b : a);
    const double w = snap ? 0.0 : std::clamp(w01, 0.0, 1.0);
    const std::size_t want =
        (static_cast<std::size_t>(gw) * gh + 7) / 8;

    if (snap || i0 == i1 || from.bits.size() == want) {
        UnpackBits(from.bits.data(), gw, gh, a8_out);
    } else {
        // Crossfade the two bitmaps as coverage.
        std::vector<std::uint8_t> a_a8, b_a8;
        UnpackBits(a.bits.size() == want ? a.bits.data() : nullptr,
                   gw, gh, &a_a8);
        UnpackBits(b.bits.size() == want ? b.bits.data() : nullptr,
                   gw, gh, &b_a8);
        a8_out->resize(a_a8.size());
        for (std::size_t i = 0; i < a_a8.size(); ++i) {
            (*a8_out)[i] = static_cast<std::uint8_t>(std::lround(
                a_a8[i] * (1.0 - w) + b_a8[i] * w));
        }
    }
    if (box_out != nullptr) {
        const double bw =
            a.w + (b.w - a.w) * w;
        const double bh =
            a.h + (b.h - a.h) * w;
        box_out->cx = a.cx + (b.cx - a.cx) * w;
        box_out->cy = a.cy + (b.cy - a.cy) * w;
        box_out->w = bw;
        box_out->h = bh;
    }
    return true;
}

bool CanvasPointToSourceFraction(const Clip& clip, double u,
                                 int canvas_w, int canvas_h,
                                 int src_w, int src_h,
                                 double nx, double ny,
                                 float* fx, float* fy) {
    if (fx == nullptr || fy == nullptr || canvas_w <= 0 ||
        canvas_h <= 0 || src_w <= 0 || src_h <= 0) {
        return false;
    }
    const Transform tr = EffectiveTransformAt(clip, u);
    if (!(tr.scale > 0.0f)) {
        return false;
    }
    // Exact inverse of WarpClipFrame's forward map on the full
    // frame: the scaled rect (dw x dh) sits top-left at
    // (offset_x*canvas_w, offset_y*canvas_h), rotated
    // rotation_deg about its center (cx, cy). The warp samples
    // u = (vx*cos + vy*sin + half_dw)/scale -> source px; dividing
    // by dw instead gives the frame fraction directly.
    const double dw = src_w * static_cast<double>(tr.scale);
    const double dh = src_h * static_cast<double>(tr.scale);
    if (dw <= 0.0 || dh <= 0.0) {
        return false;
    }
    const double cx =
        tr.offset_x * canvas_w + dw * 0.5;
    const double cy =
        tr.offset_y * canvas_h + dh * 0.5;
    const double theta =
        static_cast<double>(tr.rotation_deg) * 3.14159265358979323846 /
        180.0;
    const double cos_t = std::cos(theta);
    const double sin_t = std::sin(theta);
    const double vx = nx * canvas_w - cx;
    const double vy = ny * canvas_h - cy;
    const double ux = vx * cos_t + vy * sin_t;
    const double uy = -vx * sin_t + vy * cos_t;
    const double fxd = (ux + dw * 0.5) / dw;
    const double fyd = (uy + dh * 0.5) / dh;
    if (fxd < 0.0 || fxd > 1.0 || fyd < 0.0 || fyd > 1.0) {
        return false;   // clicked outside the clip's warped rect
    }
    *fx = static_cast<float>(fxd);
    *fy = static_cast<float>(fyd);
    return true;
}

}  // namespace XER::VideoEditor::Ai
