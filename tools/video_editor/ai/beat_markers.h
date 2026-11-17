// Beat detection -> timeline markers ("edit to the beat"). Wraps
// XER::Audio::AnalysisAI::BeatTracker (already compiled into
// XER_AudioCore) over PCM extracted by ai/audio_pcm.
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

struct BeatResult {
    std::vector<double> times;   // seconds, sorted
    double bpm = 0.0;            // estimated tempo, 0 when unknown
};

// Decodes up to `max_seconds` of audio (0 = whole file), runs the
// energy/onset tracker and returns beat times suitable for markers.
// False with `error_out` when the file has no usable audio.
bool DetectBeats(const std::string& utf8_path, double max_seconds,
                 BeatResult* out, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
