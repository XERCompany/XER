#include "audio_sync.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

#include "media_catalog.h"

namespace XER::VideoEditor {
namespace {

// Peaks come back at samples_per_peak/48kHz bucket width (see
// DecodeAudioPeaks). We decimate 4x for correlation — ~21ms cells
// smooth the envelope, which is what makes faint/distant room mics
// still lock: fine structure differs per mic distance, energy
// contour does not.
constexpr double kBucketSeconds = 256.0 / 48000.0;
constexpr int kDecimate = 4;
// Below this normalized correlation a "match" is treated as noise —
// the clip is reported but left where it sits.
constexpr double kMinConfidence = 0.30;
// Correlation cost ceiling: the sweep below is ~O(n*m) envelope-cell
// products. Multi-hour sources would otherwise pin a core for
// minutes, so above this budget both envelopes are decimated 4x per
// level (~85ms cells at level 2) and the coarse answer is refined at
// full resolution around the winning lag — the search stays bounded
// AND frame-accurate.
constexpr double kMaxCorrelationProducts = 2.0e8;
// ScanLags reports cancellation with this out-of-range confidence so
// callers can distinguish abort from a genuine no-match (-1..1).
constexpr double kCancelled = -2.0;

// Mono energy envelope of a file's peaks: sqrt of channel-averaged
// squared bucket magnitude (max(|min|,|max|)), decimated.
std::vector<float> Envelope(const AudioPeaks& peaks) {
    std::vector<float> env;
    if (peaks.channels <= 0 || peaks.buckets == 0) return env;
    const std::size_t cells =
        (peaks.buckets + kDecimate - 1) / kDecimate;
    env.assign(cells, 0.0f);
    for (std::size_t b = 0; b < peaks.buckets; ++b) {
        double e = 0.0;
        for (int c = 0; c < peaks.channels; ++c) {
            const std::size_t base =
                static_cast<std::size_t>(c) * peaks.buckets * 2 + b * 2;
            const float lo = peaks.data[base];
            const float hi = peaks.data[base + 1];
            const float a = std::max(std::fabs(lo), std::fabs(hi));
            e += a * a;
        }
        env[b / kDecimate] += static_cast<float>(
            std::sqrt(e / peaks.channels));
    }
    // DC-strip: correlation on a non-zero-mean signal is dominated by
    // the mean, which makes silent-vs-music pairs look correlated.
    double mean = 0.0;
    for (float v : env) mean += v;
    mean /= env.size();
    for (float& v : env) v -= static_cast<float>(mean);
    return env;
}

// Mean-of-4 downscale — used when the lag sweep would exceed the
// product ceiling. Zero-mean in, zero-mean out.
std::vector<float> Decimate4(const std::vector<float>& env) {
    std::vector<float> out((env.size() + 3) / 4, 0.0f);
    for (std::size_t i = 0; i < env.size(); ++i) {
        out[i / 4] += env[i];
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::size_t count =
            std::min<std::size_t>(4, env.size() - i * 4);
        out[i] /= static_cast<float>(count);
    }
    return out;
}

// Normalized cross-correlation over lags [lag_min, lag_max) — clamped
// to (-m, n] — where the shorter envelope still overlaps the longer
// by >= 25%. Returns {best lag L, coefficient} such that
// tgt[k] ~ ref[k - L]; negative L = the target's take starts LATER
// inside its own window. Confidence kCancelled = abort requested.
std::pair<int, double> ScanLags(const std::vector<float>& ref,
                                const std::vector<float>& tgt,
                                int lag_min, int lag_max,
                                const std::atomic<bool>* cancel) {
    const int n = static_cast<int>(ref.size());
    const int m = static_cast<int>(tgt.size());
    lag_min = std::max(lag_min, -m);
    lag_max = std::min(lag_max, n);
    const int min_overlap = std::min(n, m) / 4;
    int best_lag = 0;
    double best = -1.0;
    for (int lag = lag_min; lag < lag_max; ++lag) {
        if (cancel != nullptr && (lag & 0x1FF) == 0 &&
            cancel->load(std::memory_order_relaxed)) {
            return {0, kCancelled};
        }
        // overlap span: tgt index t maps to ref index t - lag.
        const int t0 = std::max(0, lag);
        const int t1 = std::min(m, n + lag);
        const int span = t1 - t0;
        if (span < min_overlap) continue;
        double num = 0.0, te = 0.0, re = 0.0;
        for (int t = t0; t < t1; ++t) {
            const double rv = ref[t - lag];
            const double tv = tgt[t];
            num += rv * tv;
            re += rv * rv;
            te += tv * tv;
        }
        const double den = std::sqrt(re * te);
        if (den <= 1e-12) continue;
        const double score = num / den;
        if (score > best) {
            best = score;
            best_lag = lag;
        }
    }
    return {best_lag, best};
}

// Bounded BestLag: decimate both envelopes until n*m fits the
// ceiling, run the full sweep at that resolution, then refine
// ±scale cells at full resolution around the coarse winner.
std::pair<int, double> BestLag(const std::vector<float>& ref,
                               const std::vector<float>& tgt,
                               const std::atomic<bool>* cancel) {
    const int n = static_cast<int>(ref.size());
    const int m = static_cast<int>(tgt.size());
    if (n < 8 || m < 8) return {0, 0.0};
    double ref_energy = 0.0;
    for (float v : ref) ref_energy += v * v;
    if (ref_energy <= 1e-12) return {0, 0.0};

    std::vector<float> r = ref;
    std::vector<float> t = tgt;
    int scale = 1;
    while (static_cast<double>(r.size()) * t.size() >
               kMaxCorrelationProducts &&
           r.size() >= 64 && t.size() >= 64) {
        r = Decimate4(r);
        t = Decimate4(t);
        scale *= 4;
    }
    auto [lag, conf] = ScanLags(r, t, -static_cast<int>(t.size()),
                                static_cast<int>(r.size()), cancel);
    if (conf <= kCancelled) return {0, kCancelled};
    lag *= scale;
    if (scale > 1) {
        // Fine pass: the coarse cell smears the lock by up to `scale`
        // cells — rescan just that window at ~21ms resolution.
        auto [fine_lag, fine_conf] =
            ScanLags(ref, tgt, lag - scale, lag + scale + 1, cancel);
        if (fine_conf <= kCancelled) return {0, kCancelled};
        // A competitive fine peak wins — correlation at the wrong
        // coarse cell can look almost as good as the true one.
        if (fine_conf > 0.0 &&
            (conf <= 0.0 || fine_conf >= conf * 0.9)) {
            lag = fine_lag;
            conf = std::max(conf, fine_conf);
        }
    }
    return {lag, std::max(0.0, conf)};
}

struct SyncSource {
    std::string clip_id;
    std::string canon_path;
    double start = 0.0;
    double in_point = 0.0;
    double rate = 1.0;
};

}  // namespace

bool ComputeAudioSyncPlan(const Project& project,
                               const std::string& reference_clip_id,
                               AudioSyncReport* report,
                               std::string* error_out,
                               const AudioSyncPlanOptions& options) {
    if (report) *report = AudioSyncReport{};
    auto cancelled = [&]() -> bool {
        if (options.cancel != nullptr &&
            options.cancel->load(std::memory_order_relaxed)) {
            if (error_out) *error_out = "audio sync cancelled";
            return true;
        }
        return false;
    };

    // Gather media clips (audio-capable until proven otherwise —
    // the peaks provider fails cleanly on video-only sources).
    std::vector<SyncSource> candidates;
    for (const auto& clip : project.clips) {
        if (clip.source_path.empty() ||
            clip.source.kind != SourceKind::Media) {
            continue;
        }
        candidates.push_back({clip.id, clip.source_path,
                              clip.timeline_start, clip.in_point,
                              clip.playback_rate});
    }
    if (candidates.empty()) {
        if (error_out) *error_out = "no media clips on the timeline";
        return false;
    }

    // Reference: requested id, else the earliest-starting clip on the
    // lowest track index (the "main" track by convention).
    auto pick_ref = [&]() -> const SyncSource* {
        if (!reference_clip_id.empty()) {
            for (const auto& c : candidates) {
                if (c.clip_id == reference_clip_id) return &c;
            }
            return nullptr;
        }
        const SyncSource* best = nullptr;
        int best_track = 1 << 30;
        for (const auto& c : candidates) {
            const Clip* clip = FindClip(project, c.clip_id);
            const int ti = clip ? clip->track : 0;
            if (ti < best_track ||
                (ti == best_track && best && c.start < best->start)) {
                best = &c;
                best_track = ti;
            }
        }
        return best;
    };
    const SyncSource* ref = pick_ref();
    if (!ref) {
        if (error_out) *error_out = "reference clip not found";
        return false;
    }

    // Envelopes once per unique source path — through the injected
    // provider (MediaPrep wait / test peaks) or the default
    // DecodeAudioPeaks subprocess path.
    std::unordered_map<std::string, std::vector<float>> envelopes;
    auto envelope_of = [&](const SyncSource& s,
                           std::vector<float>* out) -> bool {
        auto it = envelopes.find(s.canon_path);
        if (it != envelopes.end()) {
            *out = it->second;
            return !out->empty();
        }
        AudioPeaks peaks;
        bool decoded = false;
        if (options.peaks) {
            decoded = options.peaks(s.canon_path, &peaks);
        } else {
            std::string err;
            decoded = DecodeAudioPeaks(s.canon_path, &peaks, &err);
        }
        if (cancelled()) {
            envelopes[s.canon_path] = {};
            return false;
        }
        if (!decoded) {
            envelopes[s.canon_path] = {};
            return false;
        }
        envelopes[s.canon_path] = Envelope(peaks);
        *out = envelopes[s.canon_path];
        return !out->empty();
    };

    const int total = static_cast<int>(candidates.size());
    int done = 0;
    if (options.progress) options.progress(0, total);

    std::vector<float> ref_env;
    if (!envelope_of(*ref, &ref_env)) {
        if (cancelled()) return false;
        if (error_out) {
            *error_out = "reference clip has no decodable audio";
        }
        return false;
    }

    const double cell = kBucketSeconds * kDecimate;
    for (const auto& c : candidates) {
        if (cancelled()) return false;
        AudioSyncEntry e;
        e.clip_id = c.clip_id;
        if (c.clip_id == ref->clip_id) {
            if (options.progress) options.progress(++done, total);
            continue;
        }
        if (c.canon_path == ref->canon_path) {
            e.note = "same source as reference";
            if (report) report->entries.push_back(e);
            if (options.progress) options.progress(++done, total);
            continue;
        }
        std::vector<float> env;
        if (!envelope_of(c, &env)) {
            if (cancelled()) return false;
            e.note = "no audio in source";
            if (report) report->entries.push_back(e);
            if (options.progress) options.progress(++done, total);
            continue;
        }
        const auto [lag, conf] = BestLag(ref_env, env, options.cancel);
        if (conf <= kCancelled) {
            cancelled();   // fills error_out
            return false;
        }
        e.confidence = conf;
        if (conf < kMinConfidence) {
            e.note = "no confident match";
            if (report) report->entries.push_back(e);
            if (options.progress) options.progress(++done, total);
            continue;
        }
        // tgt[k] ~ ref[k-lag]: an event at ref envelope index i lands
        // in the target's own source window at i+lag, i.e. source
        // offset lag*cell. Aligning on the timeline therefore shifts
        // the clip EARLIER by lag*cell (scaled by playback rate —
        // in_point consumes source seconds at `rate` speed).
        e.delta_seconds = -lag * cell / std::max(0.05, c.rate);
        e.new_timeline_start = std::max(0.0, c.start + e.delta_seconds);
        e.matched = true;
        if (report) report->entries.push_back(std::move(e));
        if (options.progress) options.progress(++done, total);
    }

    if (report) {
        report->reference_clip_id = ref->clip_id;
        int matched = 0;
        std::string confs;
        for (const auto& e : report->entries) {
            if (!e.matched) continue;
            ++matched;
            char buf[16];
            std::snprintf(buf, sizeof buf, "%s%.2f",
                          confs.empty() ? "" : "/", e.confidence);
            confs += buf;
        }
        report->status = "audio sync: " + std::to_string(matched) +
                         " aligned";
        if (!confs.empty()) {
            report->status += " (conf " + confs + ")";
        }
        report->status +=
            ", " +
            std::to_string(report->entries.size() - matched) +
            " skipped";
    }
    return true;
}

bool ComputeAudioSyncPlanWithPeaks(
    const Project& project, const std::string& reference_clip_id,
    const AudioSyncPeaksProvider& peaks, AudioSyncReport* report,
    std::string* error_out) {
    AudioSyncPlanOptions options;
    options.peaks = peaks;
    return ComputeAudioSyncPlan(project, reference_clip_id, report,
                                error_out, options);
}

}  // namespace XER::VideoEditor
