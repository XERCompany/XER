// Offline AI bounce pipeline for the VideoEditor — the wave-2
// file-producing seam shared by "AI Upscale 4x" (media bin) and
// "Smooth slow-mo (AI)" (timeline clip), mirroring the
// xer.af.voice_isolate bounce convention: decode -> model ->
// encode to a <stem>.xer.<tag>.mp4 next to the source, which the
// caller imports back into the media catalog.
//
// Why a bounce and not a compositor path: both wave-2 models cost
// seconds-class CPU per frame — offline by definition. A bounced
// file is honest output (previewable, re-trimmable, exportable
// through the normal path) instead of a preview-only lie.
//
// Pipeline shape (all on the caller's worker thread):
//   pass A — open the decoder (audio tap when `with_audio`),
//            seek to `in_seconds`, forward-discard to the span
//            start, then count source frames inside
//            [in_seconds, out_seconds) while draining tap chunks;
//            records fps/dims.
//   pass B — re-open cleanly, re-seek, and let the FFmpeg encode
//            stream pull output frames: each provider call runs
//            the caller's transform (which pulls decoded source
//            frames through BounceSource::next), so peak video
//            memory stays O(tile + a few frames), not O(movie).
//   audio  — the tap chunks collected in pass A are re-encoded
//            verbatim through EncodeAudioVideoStream (swr converts
//            formats itself). Only offered for whole-file spans —
//            a partial span's audio wouldn't align.
//   commit — encode lands in a sibling temp file and commits
//            atomically (XER::Cache::TempPathForTarget /
//            CommitTempFile, the export engine's convention).
//
// Output naming: AiBounceOutputPath(source, tag) ->
// "<dir>/<stem>.xer.<tag>.mp4" — e.g.
// "clip.xer.upscaled.mp4", "clip.xer.slowmo2x.mp4" — suffixed
// ".2", ".3", ... when the plain name exists. The .xer. infix
// marks derived media (so a later upscale of the upscale reads
// its own provenance) and keeps the bounce out of the source's
// name.
//
// Pure worker code — never call RunVideoBounce on the UI thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace XER::VideoEditor::Ai {

struct AiBounceParams {
    std::string source_path;   // UTF-8 media path (must have video)
    std::string output_path;   // UTF-8 target — see AiBounceOutputPath
    // Source span in media seconds [in, out); 0/0 = whole file.
    // Audio is only carried on whole-file bounces.
    double in_seconds = 0.0;
    double out_seconds = 0.0;
    // Output frame count factor: 1 -> one output per decoded
    // source frame (upscale); N > 1 -> (count-1)*N + 1 outputs
    // (slow-mo interpolation between consecutive pairs).
    int out_frames_per_in = 1;
    std::string video_codec = "libx264";
    int bit_rate = 0;          // bps; 0 = bridge default (4 Mbps)
    int crf = 18;              // -1 keeps the bit_rate path
    bool with_audio = false;   // whole-file spans only
};

// One decoded source frame inside the span (packed BGRA8,
// stride == w*4).
struct AiBounceFrame {
    std::vector<std::uint8_t> bgra;
    int w = 0;
    int h = 0;
    double t = 0.0;            // source seconds of this frame
};

// What the bounce knows mid-run — handed to the transform each
// output index.
struct AiBounceContext {
    // Sequential source-frame pull INSIDE the span: fills `frame`
    // and returns true, or false at span end / fatal decode (err
    // distinguishes — false+empty err = clean EOF). Calls must be
    // strictly sequential (the decoder only moves forward); the
    // transform keeps whatever small window it needs itself.
    std::function<bool(AiBounceFrame* frame, std::string* err)>
        next_source;
    double fps = 0.0;          // source frame rate
    int src_w = 0;
    int src_h = 0;
    std::size_t source_total = 0;   // decoded frames in the span
    std::size_t out_total = 0;      // output frames to produce
};

// The caller's per-output-frame op: produce output index `i`
// (packed BGRA8, consistent dims across i) using ctx.next_source
// for decode. False + err aborts the bounce.
using AiBounceFrameFn =
    std::function<bool(std::size_t out_index,
                       const AiBounceContext& ctx,
                       std::vector<std::uint8_t>* bgra, int* w,
                       int* h, std::string* error_out)>;

struct AiBounceResult {
    std::string output_path;        // the committed file
    std::size_t source_frames = 0;
    std::size_t output_frames = 0;
    int out_w = 0;
    int out_h = 0;
    double fps = 0.0;               // output fps (== source fps)
    double duration_seconds = 0.0;  // output duration
    bool has_audio = false;
};

// ---- transform builders (model-free, smoke-testable) ----------
// 1:1 frame op (upscale): pulls exactly one source frame per
// output index and hands it to `fn`, which fills the output block
// and reports its (possibly different) dims.
using AiFrameFn = std::function<bool(
    const AiBounceFrame& src, std::vector<std::uint8_t>* out_bgra,
    int* out_w, int* out_h, std::string* error_out)>;
AiBounceFrameFn MakeFrameTransform(AiFrameFn fn);

// Pairwise interpolator (slow-mo): output index i maps to source
// pair p = i/factor at position t = (i%factor)/factor; i%factor==0
// emits the verbatim source frame. `interp(a, b, t, out, err)`
// fills the interpolated frame. Keeps a 3-frame sliding window so
// source pulls stay sequential and memory stays O(3 frames).
using AiPairInterpFn = std::function<bool(
    const AiBounceFrame& a, const AiBounceFrame& b, float t,
    std::vector<std::uint8_t>* out_bgra, std::string* error_out)>;
// `factor` matches AiBounceParams::out_frames_per_in — clamped to
// >= 1 internally (1 = passthrough, still pairwise-shaped).
AiBounceFrameFn MakePairInterpTransform(int factor,
                                        AiPairInterpFn interp);

// "<dir>/<stem>.xer.<tag>.mp4", suffixed ".2"/".3"/... until free.
// `tag` should stay lowercase-ish ("upscaled", "slowmo2x").
std::string AiBounceOutputPath(const std::string& source_utf8,
                               const std::string& tag);

// Runs the whole pipeline. `transform` produces each output frame
// on demand (see AiBounceFrameFn). False with error_out on: bad
// params, missing/dead media, decoder unavailable, an empty span,
// < 2 source frames when out_frames_per_in > 1, transform or
// encode failure, cancellation ("cancelled"), or an audio-cap
// abort — decoded audio is buffered whole-file, so spans whose
// audio would exceed the safety cap bounce video-only instead of
// dying; that case reports through `result` (has_audio=false), not
// an error.
bool RunVideoBounce(const AiBounceParams& params,
                    const AiBounceFrameFn& transform,
                    const std::function<void(float)>& progress,
                    const std::function<bool()>& should_cancel,
                    AiBounceResult* result, std::string* error_out);

}  // namespace XER::VideoEditor::Ai
