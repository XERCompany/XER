// voice_isolate.h — AI voice isolation / noise reduction for clip
// audio (the "Dialogue Separator" / "Enhance Speech" feature).
//
// Runs the staged GTCRN ONNX denoiser (assets/models/
// gtcrn_simple.onnx — MIT, Xiaobin-Rong/gtcrn streaming export
// mirrored by sherpa-onnx) through the shared dual-backend
// XER::AI::Onnx::Session. The model is a 16 kHz mono streaming
// enhancer: it consumes one 512-pt STFT frame per Run (sqrt-Hann
// analysis, hop 256) and carries three recurrent caches; the
// STFT/ISTFT + resampling shell lives here.
//
// Consumed by the builtin audio effect `xer.af.voice_isolate`
// (builtin_fx.cc) — exposed through the Media Bin "Audio FX ->"
// bounce workflow, which is the supported clip-audio-fx path
// (the realtime mixer chain only hosts VST3/script: nodes).
#pragma once

#include <cstdint>
#include <string>

namespace XER::VideoEditor::VoiceIsolate {

// Model rate the GTCRN graph is trained for; all I/O is resampled
// to/from this rate internally.
inline constexpr int kModelRate = 16000;
inline constexpr const char* kModelFile = "gtcrn_simple.onnx";

// Resolved absolute path of the staged model ("" when not installed —
// same probe order as the lab ops: $XER_AI_MODELS_DIR,
// <exe>/assets/models, <exe>/models). Cheap; no runtime load.
std::string ModelPath();

// True when the model file is staged AND an ONNX backend reports
// loadable. Cheap (no session load) — used by the smoke checks to
// gate the real-inference assertions.
bool Available();

// Denoises interleaved float PCM in place. Each channel is processed
// independently (fresh model state per channel), resampled to 16 kHz,
// enhanced frame-by-frame, resampled back, then blended with the dry
// signal by `amount` (0..1 — clamped; 0 is a verified no-op).
// Returns false with `error_out` set when the model/backend is
// unavailable, the model rejects the frame contract, or the input is
// invalid — the buffer is left untouched on failure.
bool ProcessBuffer(float* samples, std::int32_t frames,
                   std::int32_t channels, std::int32_t sample_rate,
                   float amount, std::string* error_out);

}  // namespace XER::VideoEditor::VoiceIsolate
