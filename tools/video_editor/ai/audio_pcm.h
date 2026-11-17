// Audio PCM extraction for AI features (whisper transcription, beat
// detection). Uses the editor's IMediaDecoder audio tap — no direct
// FFmpeg dependency — and converts every common sample format to
// interleaved float mono with a linear resampler.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

struct Pcm {
    std::vector<float> samples;  // mono, interleaved
    int sample_rate = 0;         // source rate after mixdown
    double duration_seconds = 0.0;
};

// Decodes the whole audio stream of `utf8_path`, mixes down to mono and
// resamples to `target_rate` (0 keeps the source rate). `max_seconds`
// caps the decode (0 = whole file) — transcription of long media stays
// bounded. Returns false with `error_out` when the file has no audio or
// cannot be decoded.
bool ExtractMonoPcm(const std::string& utf8_path, int target_rate,
                    double max_seconds, Pcm* out, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
