// Silero VAD (voice-activity detection) for the VideoEditor — the
// speech-probability model behind the VAD silence-cut upgrade
// (docs/wiki/research/2026-11-23-ai-features-wave2.md §8). Wraps the
// staged silero_vad.onnx on the shared XER::AI::Onnx session —
// same runtime story as voice_isolate.cc / lab/ai_ops.cc: a staged
// file in <exe>/assets/models (Lab::FindAiModel) + whatever backend
// loads it (ORT handles the model's top-level If; cv::dnn reports
// "rejected" and the callers fall back to the RMS path).
//
// Contract (verified 2026-11-23 against onnx-community/silero-vad,
// fp32, 2.2 MB — assets/models/README.md):
//   input  f32  [B, N]      — mono f32 PCM at 16 kHz, N = 512
//   state  f32  [2, B, 128] — LSTM-ish streaming state; zero-init,
//                             then the previous call's stateN verbatim
//   sr     i64  scalar      — 16000 (an 8 kHz branch exists in the
//                             same file; only the 16 kHz path is used)
//   output f32  [B, 1]      — speech probability for the window
//   stateN f32  [2, B, 128] — carry into the next window
//
// SileroVad is one streaming detector = one session per instance;
// thread-hostile like the runtimes it wraps (scan on the worker,
// never the UI thread). DetectSpeech is the whole-file convenience
// the silence detector calls.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::AI::Onnx {
class Session;
}

namespace XER::VideoEditor::Ai {

inline constexpr const char* kSileroVadFile = "silero_vad.onnx";
inline constexpr int kVadSampleRate = 16000;
// Window the model consumes per Run: 512 samples = 32 ms @16 kHz
// (the onnx-community export's only accepted 16 kHz frame size;
// a short tail is zero-padded to a full window).
inline constexpr int kVadWindowSamples = 512;

// Resolves the staged model (XER_AI_MODELS_DIR / assets/models /
// models) — empty when silero_vad.onnx is not installed.
std::string SileroVadModel();
// Model staged AND an ONNX backend loadable — the cheap gate the
// silence detector uses before choosing VAD over the RMS fallback.
bool SileroVadAvailable();

// One streaming Silero detector. Loads its own session at Load;
// state carries across ProcessWindow calls until Reset (a new file
// or a time gap resets it — never stream across unrelated audio).
class SileroVad {
public:
    SileroVad();
    ~SileroVad();
    SileroVad(const SileroVad&) = delete;
    SileroVad& operator=(const SileroVad&) = delete;

    // Loads `model_path` on the first available ONNX backend.
    bool Load(const std::string& model_path,
              std::string* error_out = nullptr);
    bool IsLoaded() const;

    // Zeroes the carried streaming state (fresh audio).
    void Reset();

    // Runs one 512-sample window of 16 kHz mono f32 PCM through the
    // model, updates the carried state, and writes the window's
    // speech probability (0..1) to *prob_out. `samples` must point
    // at kVadWindowSamples floats — callers pad short tails.
    bool ProcessWindow(const float* samples, float* prob_out,
                       std::string* error_out = nullptr);

private:
    std::unique_ptr<XER::AI::Onnx::Session> session_;
    std::vector<float> state_;   // [2,1,128], carried across windows
    std::int64_t sr_ = kVadSampleRate;
    std::vector<std::int64_t> sr_dims_;
};

// Speech-window hysteresis + span shaping. `speech_on` starts a
// speech run, `speech_off` ends it (the gap between the two keeps
// borderline windows inside the current run — the same hysteresis
// silero's own examples ship). Runs shorter than min_speech are
// dropped; neighboring runs separated by less than min_silence merge
// (a brief breath never splits a sentence).
struct VadSpeechParams {
    float speech_on = 0.5f;      // prob >= this opens a speech run
    float speech_off = 0.35f;    // prob <= this closes it
    double min_speech = 0.25;    // s: shorter speech runs are dropped
    double min_silence = 0.3;    // s: shorter speech gaps merge
};

// One contiguous speech span, SOURCE seconds.
struct SpeechSpan {
    double t0 = 0.0;
    double t1 = 0.0;
    float mean_prob = 0.0f;      // mean window prob across the span
};

// Streams `n` samples of 16 kHz mono f32 PCM through `vad` in
// kVadWindowSamples windows (the short tail is zero-padded; its
// span is truncated to the real duration) and reduces the per-window
// probabilities into SpeechSpan list + per-window probs (one entry
// per 32 ms window — `probs_out` may be nullptr when only spans are
// wanted). False with error_out on a null arg or an unloaded
// detector / inference failure.
bool DetectSpeech(SileroVad& vad, const float* samples,
                  std::size_t n, const VadSpeechParams& params,
                  std::vector<SpeechSpan>* spans_out,
                  std::vector<float>* probs_out,
                  std::string* error_out);

}  // namespace XER::VideoEditor::Ai
