#include "audio_sync.h"

#include <algorithm>
#include <cmath>
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

// Normalized cross-correlation over all lags where the shorter
// envelope still overlaps the longer by >= 25%. Returns the best lag
// L (in cells) such that tgt[k] ~ ref[k - L], plus the coefficient.
// Negative L = the target's take starts LATER inside its own window.
std::pair<int, double> BestLag(const std::vector<float>& ref,
                               const std::vector<float>& tgt) {
    const int n = static_cast<int>(ref.size());
    const int m = static_cast<int>(tgt.size());
    if (n < 8 || m < 8) return {0, 0.0};
    double ref_energy = 0.0;
    for (float v : ref) ref_energy += v * v;
    if (ref_energy <= 1e-12) return {0, 0.0};

    int best_lag = 0;
    double best = -1.0;
    for (int lag = -m; lag < n; ++lag) {
        // overlap span: tgt index t maps to ref index t - lag.
        const int t0 = std::max(0, lag);
        const int t1 = std::min(m, n + lag);
        const int span = t1 - t0;
        if (span < std::min(n, m) / 4) continue;
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
    return {best_lag, std::max(0.0, best)};
}

struct SyncSource {
    std::string clip_id;
    std::string canon_path;
    double start = 0.0;
    double in_point = 0.0;
    double rate = 1.0;
    bool audio_track = false;   // bound: has_video=false for audio tracks
};

}  // namespace

bool ComputeAudioSyncPlan(const Project& project,
                               const std::string& reference_clip_id,
                               AudioSyncReport* report,
                               std::string* error_out) {
    if (report) *report = AudioSyncReport{};

    // Gather media clips (audio-capable until proven otherwise —
    // DecodeAudioPeaks fails cleanly on video-only sources).
    std::vector<SyncSource> candidates;
    for (const auto& clip : project.clips) {
        if (clip.source_path.empty() ||
            clip.source.kind != SourceKind::Media) {
            continue;
        }
        bool audio_track = false;
        if (clip.track >= 0 &&
            clip.track < static_cast<int>(project.tracks.size())) {
            audio_track = project.tracks[static_cast<std::size_t>(
                              clip.track)]
                              .kind == TrackKind::Audio;
        }
        candidates.push_back({clip.id, clip.source_path,
                              clip.timeline_start, clip.in_point,
                              clip.playback_rate, audio_track});
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

    // Decode envelopes once per unique source path.
    std::unordered_map<std::string, std::vector<float>> envelopes;
    auto envelope_of = [&](const SyncSource& s,
                           std::vector<float>* out) -> bool {
        auto it = envelopes.find(s.canon_path);
        if (it != envelopes.end()) {
            *out = it->second;
            return !out->empty();
        }
        AudioPeaks peaks;
        std::string err;
        if (!DecodeAudioPeaks(s.canon_path, &peaks, &err)) {
            envelopes[s.canon_path] = {};
            return false;
        }
        envelopes[s.canon_path] = Envelope(peaks);
        *out = envelopes[s.canon_path];
        return !out->empty();
    };

    std::vector<float> ref_env;
    if (!envelope_of(*ref, &ref_env)) {
        if (error_out) {
            *error_out = "reference clip has no decodable audio";
        }
        return false;
    }

    const double cell = kBucketSeconds * kDecimate;
    for (const auto& c : candidates) {
        AudioSyncEntry e;
        e.clip_id = c.clip_id;
        if (c.clip_id == ref->clip_id) continue;
        if (c.canon_path == ref->canon_path) {
            e.note = "same source as reference";
            if (report) report->entries.push_back(e);
            continue;
        }
        std::vector<float> env;
        if (!envelope_of(c, &env)) {
            e.note = "no audio in source";
            if (report) report->entries.push_back(e);
            continue;
        }
        const auto [lag, conf] = BestLag(ref_env, env);
        e.confidence = conf;
        if (conf < kMinConfidence) {
            e.note = "no confident match";
            if (report) report->entries.push_back(e);
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
    }

    if (report) {
        report->reference_clip_id = ref->clip_id;
        int matched = 0;
        for (const auto& e : report->entries) matched += e.matched;
        report->status = "audio sync: " + std::to_string(matched) +
                         " alignable, " +
                         std::to_string(report->entries.size() - matched) +
                         " skipped";
    }
    return true;
}

}  // namespace XER::VideoEditor
