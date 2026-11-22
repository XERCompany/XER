// Silence / loudness analysis over extracted PCM (ai/audio_pcm.h) —
// the data layer behind the extension-service calls
// "ai.detect_silences" and "ai.loudness" (sdk/ve.core.h v3). All
// functions are pure worker code: no shared state, safe on any
// thread; the dispatcher opts into the UI thread explicitly via the
// documented blocking sync contract.
#pragma once

#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

// One contiguous silent span, SOURCE seconds.
struct SilenceSpan {
    double t0 = 0.0;
    double t1 = 0.0;
    float rms_db = -120.0f;   // mean window RMS across the span (dBFS)
};

// One analysis window of the short-term RMS envelope.
struct AudioEnvelopeWindow {
    double t = 0.0;           // window start, source seconds
    float rms_db = -120.0f;   // dBFS
    float peak_db = -120.0f;  // dBFS
};

struct SilenceDetectParams {
    double threshold_db = -40.0;  // windows below this count as silent
    double min_silence = 0.5;     // spans shorter than this are dropped
    double window_ms = 50.0;      // RMS window length (10..500)
    double max_seconds = 300.0;   // decode cap (0 = whole file)
};

struct SilenceResult {
    std::vector<SilenceSpan> silences;
    std::vector<AudioEnvelopeWindow> envelope;  // filled on request
    double duration_seconds = 0.0;
    double threshold_db = -40.0;
};

// Decodes `utf8_path` to 16 kHz mono PCM (ExtractMonoPcm), measures a
// short-term RMS envelope and merges below-threshold windows into
// silence spans. `with_envelope` also fills result.envelope (one
// window_ms row per hop — can be a few thousand entries on long
// media; the caller opts in). False with `error_out` when the file
// has no decodable audio.
bool DetectSilences(const std::string& utf8_path,
                    const SilenceDetectParams& params,
                    bool with_envelope, SilenceResult* out,
                    std::string* error_out);

struct LoudnessResult {
    double integrated_lufs = 0.0;   // EBU-style gated loudness
    double true_peak_dbfs = -120.0;
    double duration_seconds = 0.0;
};

// Integrated loudness + true peak of a media file's audio
// (LoudnessMeterLUFS over the whole decode). False with `error_out`
// when the file has no decodable audio.
bool MeasureLoudness(const std::string& utf8_path, double max_seconds,
                     LoudnessResult* out, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
