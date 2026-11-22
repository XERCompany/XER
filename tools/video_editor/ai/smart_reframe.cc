// Implementation of ai/smart_reframe.h — subject scan + crop geometry.
// The decode loop mirrors ai_tags.cc's ScanClipForObjects (keyframe
// seek + bounded forward discard); inference runs on a box-averaged
// ~640px-wide copy for speed, and a compact 16x16 luma/channel
// signature (same recipe as ai/scene_detect.cc) flags hard cuts so
// the smoother can snap at edits.
#include "ai/smart_reframe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <string>
#include <vector>

#include "ai_tags.h"
#include "content/ai/find_object/find_object.h"
#include "media_decoder.h"

namespace XER::VideoEditor::Ai {
namespace {

namespace FindObj = XER::AI::FindObject;

// ---------------------------------------------------------------------------
// Cheap frame helpers (worker-side; operate on the downscaled copy)

// Box-averaged BGRA downscale to exactly dst_w x dst_h — every dst
// pixel averages its source rect, so noisy sources survive the
// shrink better than a nearest-sample pick.
std::vector<std::uint8_t> DownscaleBgra(const std::vector<std::uint8_t>& src,
                                        int src_w, int src_h, int dst_w,
                                        int dst_h) {
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

// Scene-cut signature: 16x16 luma grid + per-channel means — the same
// recipe ai/scene_detect.cc uses on full frames, applied here to the
// already-small inference copy.
constexpr int kGrid = 16;

struct Signature {
    float luma[kGrid * kGrid]{};
    float mean[3]{};   // B, G, R channel means 0..255
};

void ComputeSignature(const std::uint8_t* bgra, int w, int h,
                      Signature* out) {
    std::vector<float> acc(static_cast<size_t>(kGrid * kGrid), 0.0f);
    std::vector<int> cnt(static_cast<size_t>(kGrid * kGrid), 0);
    double mb = 0.0, mg = 0.0, mr = 0.0;
    const long long total = static_cast<long long>(w) * h;
    for (int y = 0; y < h; ++y) {
        const int gy = std::min(kGrid - 1, y * kGrid / h);
        const std::uint8_t* row = bgra + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const int gx = std::min(kGrid - 1, x * kGrid / w);
            const size_t cell = static_cast<size_t>(gy) * kGrid + gx;
            const float b = static_cast<float>(row[x * 4 + 0]);
            const float g = static_cast<float>(row[x * 4 + 1]);
            const float r = static_cast<float>(row[x * 4 + 2]);
            acc[cell] += 0.114f * b + 0.587f * g + 0.299f * r;
            ++cnt[cell];
            mb += b;
            mg += g;
            mr += r;
        }
    }
    for (size_t i = 0; i < acc.size(); ++i) {
        out->luma[i] =
            cnt[i] > 0 ? acc[i] / static_cast<float>(cnt[i]) : 0.0f;
    }
    if (total > 0) {
        out->mean[0] = static_cast<float>(mb / total);
        out->mean[1] = static_cast<float>(mg / total);
        out->mean[2] = static_cast<float>(mr / total);
    }
}

float SignatureDistance(const Signature& a, const Signature& b) {
    double luma = 0.0;
    for (int i = 0; i < kGrid * kGrid; ++i) {
        luma += std::fabs(a.luma[i] - b.luma[i]);
    }
    luma /= (kGrid * kGrid * 255.0);
    double chroma = 0.0;
    for (int i = 0; i < 3; ++i) {
        chroma += std::fabs(a.mean[i] - b.mean[i]);
    }
    chroma /= (3.0 * 255.0);
    return static_cast<float>(0.8 * luma + 0.2 * chroma);
}

// Primary-subject policy: a real "person" label wins outright; with
// generic class_N labels (no .names file) COCO person is class 1 on
// DETR exports (logit index == COCO id) and 0 on YOLO exports.
bool IsPerson(const FindObj::Detection& d) {
    if (!d.label.empty() && d.label.rfind("class_", 0) != 0) {
        std::string low = d.label;
        std::transform(low.begin(), low.end(), low.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return low == "person";
    }
    return d.class_id == 0 || d.class_id == 1;
}

const FindObj::Detection* PickSubject(
    const std::vector<FindObj::Detection>& dets) {
    const FindObj::Detection* person = nullptr;
    const FindObj::Detection* largest = nullptr;
    double largest_area = -1.0;
    for (const FindObj::Detection& d : dets) {
        const double area =
            static_cast<double>(d.w) * static_cast<double>(d.h);
        if (area > largest_area) {
            largest_area = area;
            largest = &d;
        }
        if (IsPerson(d) &&
            (person == nullptr || d.confidence > person->confidence)) {
            person = &d;
        }
    }
    return person != nullptr ? person : largest;
}

// ---------------------------------------------------------------------------
// Smoothed subject center

struct Center {
    double x = 0.5;
    double y = 0.5;
};

// Causal smoother over the sample stream: each sample's raw center is
// folded into the running estimate by an EMA whose horizon `tau`
// stretches with `smoothing`, then the step is capped to a bounded
// pan velocity `vmax` (fractions of frame per second). Samples flagged
// scene_boundary relax both knobs — a cut re-centers quickly instead
// of gliding through unrelated content. Between samples the smoothed
// endpoints lerp; ends hold.
Center SmoothedCenterAt(const ClipReframe& track, double t,
                        double smoothing) {
    const std::vector<ReframeSample>& s = track.samples;
    if (s.empty()) {
        return {};
    }
    const double tau = 0.15 + (2.5 - 0.15) * smoothing;
    const double vmax = 3.0 + (0.35 - 3.0) * smoothing;
    double sm_x = s.front().cx;
    double sm_y = s.front().cy;
    double prev_t = s.front().t;
    double last_x = sm_x, last_y = sm_y;
    for (std::size_t i = 1; i < s.size(); ++i) {
        const ReframeSample& cur = s[i];
        const double dt = std::max(0.0, cur.t - prev_t);
        const double alpha =
            cur.scene_boundary
                ? 1.0
                : (tau > 0.0 ? std::clamp(dt / tau, 0.0, 1.0) : 1.0);
        double dx = (cur.cx - sm_x) * alpha;
        double dy = (cur.cy - sm_y) * alpha;
        const double cap =
            vmax * dt * (cur.scene_boundary ? 8.0 : 1.0);
        const double len = std::hypot(dx, dy);
        if (len > cap && len > 1e-9) {
            dx *= cap / len;
            dy *= cap / len;
        }
        sm_x += dx;
        sm_y += dy;
        if (cur.t >= t) {
            const double span = cur.t - prev_t;
            const double w = span > 1e-9 ? (t - prev_t) / span : 1.0;
            const double cw = std::clamp(w, 0.0, 1.0);
            return {last_x + cw * (sm_x - last_x),
                    last_y + cw * (sm_y - last_y)};
        }
        last_x = sm_x;
        last_y = sm_y;
        prev_t = cur.t;
    }
    return {sm_x, sm_y};
}

}  // namespace

ReframeWindow ReframeMaxWindow(int frame_w, int frame_h, double ratio) {
    if (frame_w <= 0 || frame_h <= 0 || !(ratio > 0.0)) {
        return {};
    }
    const double src_aspect =
        static_cast<double>(frame_w) / static_cast<double>(frame_h);
    ReframeWindow win;
    if (ratio >= src_aspect) {
        win.w = static_cast<double>(frame_w);
        win.h = win.w / ratio;
    } else {
        win.h = static_cast<double>(frame_h);
        win.w = win.h * ratio;
    }
    win.x = (frame_w - win.w) * 0.5;
    win.y = (frame_h - win.h) * 0.5;
    return win;
}

ReframeWindow ReframeWindowAt(const ClipReframe& track, double t,
                              int frame_w, int frame_h, double ratio,
                              double smoothing) {
    ReframeWindow win = ReframeMaxWindow(frame_w, frame_h, ratio);
    if (win.w <= 0.0 || win.h <= 0.0) {
        return win;
    }
    const Center c =
        SmoothedCenterAt(track, t, std::clamp(smoothing, 0.0, 1.0));
    win.x = std::clamp(c.x * frame_w - win.w * 0.5, 0.0,
                       std::max(0.0, frame_w - win.w));
    win.y = std::clamp(c.y * frame_h - win.h * 0.5, 0.0,
                       std::max(0.0, frame_h - win.h));
    return win;
}

bool ScanClipSubject(const Clip& clip, const std::string& model_path,
                     const std::string& labels_path,
                     const ReframeScanParams& params,
                     const std::function<void(float)>& progress,
                     const std::function<bool()>& should_cancel,
                     ClipReframe* out, std::string* error_out) {
    if (out == nullptr) return false;
    out->samples.clear();
    if (clip.source_path.empty() || clip.is_adjustment) {
        if (error_out) *error_out = "clip has no media to scan";
        return false;
    }
    if (model_path.empty()) {
        if (error_out) *error_out = FindObjectModelHint();
        return false;
    }

    FindObj::Detector detector;
    std::string err;
    if (!detector.Load(model_path, labels_path, "", &err)) {
        if (error_out) *error_out = err;
        return false;
    }

    MediaDecoderPtr decoder = MakeBridgeDecoder();
    if (decoder == nullptr) {
        if (error_out) *error_out = "no media decoder";
        return false;
    }
    if (!decoder->Open(clip.source_path, &err)) {
        if (error_out) *error_out = "decode open: " + err;
        return false;
    }
    if (decoder->GetWidth() <= 0 || decoder->GetHeight() <= 0) {
        if (error_out) *error_out = "media has no video stream";
        return false;
    }

    const double rate =
        clip.playback_rate > 0.0 ? clip.playback_rate : 1.0;
    const double span_src = clip.duration * rate;
    const double t_begin = std::max(0.0, clip.in_point);
    const double t_end = t_begin + std::min(span_src, params.max_seconds);
    const double step = params.sample_step > 0.05 ? params.sample_step : 0.5;
    const int samples =
        static_cast<int>(std::ceil((t_end - t_begin) / step));
    if (samples <= 0) {
        if (error_out) *error_out = "clip span is empty";
        return false;
    }

    FindObj::Options opts;
    opts.confidence_threshold = params.confidence_threshold;
    opts.nms_threshold = params.nms_threshold;

    std::vector<std::uint8_t> bgra;
    std::vector<std::uint8_t> kept;
    std::vector<std::uint8_t> small;
    int width = 0, height = 0;
    Signature prev;
    bool have_prev = false;
    double last_cut = -1e9;
    for (int i = 0; i < samples; ++i) {
        if (should_cancel != nullptr && should_cancel()) {
            if (error_out) *error_out = "cancelled";
            return false;
        }
        const double t = t_begin + i * step;
        decoder->Seek(t);   // keyframe-backward; forward-discard below
        constexpr int kMaxForwardFrames = 300;
        kept.clear();
        int kept_w = 0, kept_h = 0;
        for (int f = 0; f < kMaxForwardFrames &&
                        decoder->NextFrame(&bgra, &width, &height);
             ++f) {
            bgra.swap(kept);
            kept_w = width;
            kept_h = height;
            if (decoder->GetPositionSeconds() >= t) break;
        }
        if (kept.empty() || kept_w <= 0 || kept_h <= 0) {
            continue;   // undecodable probe — interpolation bridges it
        }
        // Inference + signature run on the shrunk copy; detection
        // boxes normalize to frame fractions either way.
        int dw = kept_w, dh = kept_h;
        if (params.detect_width > 0 && kept_w > params.detect_width) {
            dw = params.detect_width;
            dh = std::max(1, static_cast<int>(
                                 std::llround(
                                     kept_h * (static_cast<double>(dw) /
                                               kept_w))));
        }
        const std::uint8_t* pixels = kept.data();
        int pw = kept_w, ph = kept_h;
        if (dw != kept_w || dh != kept_h) {
            small = DownscaleBgra(kept, kept_w, kept_h, dw, dh);
            pixels = small.data();
            pw = dw;
            ph = dh;
        }
        Signature sig;
        ComputeSignature(pixels, pw, ph, &sig);
        const bool boundary =
            have_prev && SignatureDistance(prev, sig) >
                             params.scene_threshold &&
            t - last_cut >= params.min_scene;
        if (boundary) last_cut = t;
        prev = sig;
        have_prev = true;

        const auto dets = detector.Detect(pixels, pw, ph, pw * 4, opts);
        if (const FindObj::Detection* subject = PickSubject(dets);
            subject != nullptr) {
            ReframeSample s;
            s.t = t;
            const double fw = static_cast<double>(pw);
            const double fh = static_cast<double>(ph);
            s.cx = static_cast<float>(
                std::clamp((subject->x + subject->w * 0.5) / fw, 0.0,
                           1.0));
            s.cy = static_cast<float>(
                std::clamp((subject->y + subject->h * 0.5) / fh, 0.0,
                           1.0));
            s.w = static_cast<float>(
                std::clamp(subject->w / fw, 0.0, 1.0));
            s.h = static_cast<float>(
                std::clamp(subject->h / fh, 0.0, 1.0));
            s.confidence = subject->confidence;
            s.scene_boundary = boundary;
            out->samples.push_back(s);
        }
        if (progress) {
            progress(static_cast<float>(i + 1) /
                     static_cast<float>(samples));
        }
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
