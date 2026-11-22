// Implementation of ai/scene_detect.h — signature-diff scene cuts.
// The per-sample decode loop mirrors ai_tags.cc's ScanClipForObjects
// (keyframe seek + bounded forward discard), the signature itself is
// a 16x16 luma grid plus per-channel means — cheap enough that a
// whole movie costs about as much as its poster pass.
#include "ai/scene_detect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "media_decoder.h"

namespace XER::VideoEditor::Ai {
namespace {

constexpr int kGrid = 16;   // luma signature grid (kGrid*kGrid cells)

struct Signature {
    float luma[kGrid * kGrid]{};
    float mean[3]{};        // B, G, R channel means 0..255
};

// BGRA8 frame -> downsampled signature (box average into the grid).
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
            mb += b; mg += g; mr += r;
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

// Normalized signature distance: 80% mean |luma delta| + 20% channel
// mean drift, both mapped into 0..1.
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

}  // namespace

bool DetectScenes(const std::string& utf8_path,
                  const SceneDetectParams& params,
                  const std::function<void(float)>& progress,
                  const std::function<bool()>& should_cancel,
                  std::vector<SceneCut>* out, std::string* error_out) {
    if (out == nullptr) return false;
    out->clear();

    MediaDecoderPtr decoder = MakeBridgeDecoder();
    if (decoder == nullptr) {
        if (error_out) *error_out = "no media decoder";
        return false;
    }
    std::string err;
    if (!decoder->Open(utf8_path, &err)) {
        if (error_out) *error_out = "decode open: " + err;
        return false;
    }
    if (decoder->GetWidth() <= 0 || decoder->GetHeight() <= 0) {
        if (error_out) *error_out = "media has no video stream";
        return false;
    }
    double duration = decoder->GetDurationSeconds();
    if (duration <= 0.0) duration = params.max_seconds;
    const double t_end =
        params.max_seconds > 0.0 ? std::min(duration, params.max_seconds)
                                 : duration;
    const double step =
        params.sample_step > 0.05 ? params.sample_step : 0.25;
    const int samples =
        static_cast<int>(std::ceil(t_end / step));
    if (samples <= 0) {
        if (error_out) *error_out = "media span is empty";
        return false;
    }

    std::vector<std::uint8_t> bgra;
    std::vector<std::uint8_t> kept;
    int width = 0, height = 0;
    Signature prev;
    bool have_prev = false;
    double last_cut = -1e9;
    for (int i = 0; i < samples; ++i) {
        if (should_cancel != nullptr && should_cancel()) {
            if (error_out) *error_out = "cancelled";
            return false;
        }
        const double t = i * step;
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
        if (kept.empty() || kept_w <= 0 || kept_h <= 0) continue;
        Signature sig;
        ComputeSignature(kept.data(), kept_w, kept_h, &sig);
        if (have_prev) {
            const float dist = SignatureDistance(prev, sig);
            if (dist > params.threshold &&
                t - last_cut >= params.min_scene) {
                SceneCut cut;
                cut.t = t;
                cut.strength = dist;
                out->push_back(cut);
                last_cut = t;
            }
        }
        prev = sig;
        have_prev = true;
        if (progress) {
            progress(static_cast<float>(i + 1) /
                     static_cast<float>(samples));
        }
    }
    return true;
}

}  // namespace XER::VideoEditor::Ai
