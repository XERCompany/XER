// Speech-to-text via whisper.cpp / ggml — Premiere "Speech to Text"
// style transcription that turns media dialogue into timeline markers.
// Runtime-probed like lab/mkl_accel: the DLL (libwhisper-1.dll /
// whisper.dll) and the ggml-* model file are resolved at run time, so
// the editor ships and runs fine without them — the feature simply
// reports itself unavailable.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

struct TranscriptSegment {
    double t0 = 0.0;       // seconds
    double t1 = 0.0;       // seconds
    std::string text;      // UTF-8, already trimmed
};

// DLL resolved? (says nothing about the model file)
bool WhisperAvailable(std::string* detail = nullptr);

// Finds a usable ggml whisper model: $XER_WHISPER_MODEL, then
// <exe_dir>/models/, then %USERPROFILE%\.cache\whisper\ — first
// ggml*.bin wins. Empty when none found.
std::string FindWhisperModel();

// Transcribes `samples` (16 kHz mono float) with `model_path`.
// `progress` receives 0..1 callbacks from the worker thread when the
// backend reports them. False with `error_out` on failure.
bool Transcribe(const std::vector<float>& samples,
                const std::string& model_path,
                const std::function<void(float)>& progress,
                std::vector<TranscriptSegment>* out,
                std::string* error_out);

}  // namespace XER::VideoEditor::Ai
