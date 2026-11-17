#include "ai/beat_markers.h"

#include <algorithm>

#include "ai/audio_pcm.h"
#include "audio/analysis_ai/anal_beat_tracker.h"

namespace XER::VideoEditor::Ai {

bool DetectBeats(const std::string& utf8_path, double max_seconds,
                 BeatResult* out, std::string* error_out) {
    if (out == nullptr) return false;
    out->times.clear();
    out->bpm = 0.0;

    Pcm pcm;
    // Keep the source rate — the tracker's spectral-flux windowing is
    // rate-aware via the sample_rate argument.
    if (!ExtractMonoPcm(utf8_path, 0, max_seconds, &pcm, error_out)) {
        return false;
    }
    if (pcm.samples.size() < 4096) {
        if (error_out) *error_out = "audio too short for beat analysis";
        return false;
    }

    XER::Audio::AnalysisAI::BeatTracker tracker;
    if (!tracker.Analyze(pcm.samples.data(), pcm.samples.size(),
                         pcm.sample_rate)) {
        if (error_out) *error_out = "beat analysis failed";
        return false;
    }
    for (const auto& ev : tracker.GetBeatEvents()) {
        out->times.push_back(ev.time_seconds);
    }
    out->bpm = tracker.GetEstimatedBPM();
    if (out->times.empty()) {
        if (error_out) *error_out = "no beats detected";
        return false;
    }
    std::sort(out->times.begin(), out->times.end());
    return true;
}

}  // namespace XER::VideoEditor::Ai
