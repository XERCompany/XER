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

// A single whitespace-delimited word inside a segment, with its own
// source-time span. Populated from whisper token timestamps when the
// probed libwhisper exports the token API
// (whisper_full_n_tokens + whisper_full_get_token_*); older DLLs
// simply leave TranscriptSegment::words empty and callers fall back
// to proportional distribution (TranscriptWordsFromSegments).
struct TranscriptWordSpan {
    double t0 = 0.0;       // seconds
    double t1 = 0.0;       // seconds, >= t0
    std::string text;      // single token, no whitespace
};

struct TranscriptSegment {
    double t0 = 0.0;       // seconds
    double t1 = 0.0;       // seconds
    std::string text;      // UTF-8, already trimmed
    // Real word timings when token timestamps ran — empty means
    // "segment span only" (proportional fallback everywhere else).
    std::vector<TranscriptWordSpan> words;
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

// Safety bound on the audio span decoded for one transcription,
// seconds (0 = uncapped). ExtractMonoPcm materializes the whole span
// as 16 kHz mono float PCM (~3.7 MiB per minute) and whisper then
// walks it in a single job, so an unbounded file would pin real
// memory + the AI worker — the cap is intentional, not vestigial.
// Default 60 minutes; the env var XER_TRANSCRIBE_MAX_MINUTES
// overrides it (parsed as a double: <= 0 uncaps, an unparseable
// value falls back to the default rather than silently un-capping).
// Read live on every call so tests and tools can flip it per run.
double TranscribeMaxSeconds();

}  // namespace XER::VideoEditor::Ai
