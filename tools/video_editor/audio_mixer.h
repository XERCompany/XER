// Timeline -> PCM audio mixer for the VideoEditor core (XER Studio
// Phase 1C). UI-free, single consumer thread — GetChunk calls must be
// serialized (the export worker owns the instance). Init() snapshots
// the project (copies it); decoder sessions open lazily on the first
// chunk overlapping a clip's extended audio range and drain forward to
// EOF. GetChunk is random-access in signature (the mux may interleave
// audio/video provider calls) but chunk production is strictly
// sequential internally — callers must request each index exactly
// once in non-decreasing order (re-requesting the last index returns
// the cached chunk).
//
// Output contract (fixed by the FFmpeg encode bridge): interleaved
// signed-16 little-endian stereo at options.sample_rate —
// sample_rate=48000, channels=2, planar=false, sample_format="s16".
// `pts`/`best_effort_timestamp` are debug aids only; the mux
// interleaves by cumulative sample_count/sample_rate.
//
// Per clip the mixer owns a SEPARATE IMediaDecoder session with
// EnableAudioTap(true) called BEFORE Open() — distinct from the
// compositor's video decoder (two demux sessions per A/V file;
// spec-mandated pump cost). Audio is decoded at the source rate into
// a bounded media-time-indexed stereo-float window, then mixed on
// demand: per output sample, each contributing clip maps timeline
// time -> source media time (in_point + (tau - timeline_start)),
// linear-interpolates in media time, and applies gain_db, fade
// envelopes and transition crossfade weights. Resampling is thus
// implicit (media-time interpolation), never a separate pass.
//
// Sync notes (the primary risk area): audio-only containers have no
// usable GetPositionSeconds — all timing comes from AudioFrameInfo
// timestamps * GetAudioTimeBase(), with GetAudioStartSeconds() as the
// continuity anchor when pts are missing (AAC priming / stream start
// offsets). Decoder duration/FPS are NOT trusted for clip bounds —
// timeline/project clip bounds rule; decoder duration only clamps
// source reads when positive. Sparse/missing pts produce silence
// gaps (never shifted audio); EOF produces silence (never a frozen
// last sample).
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "media_decoder.h"     // IMediaDecoder + MediaDecoderFactory
#include "timeline_model.h"    // Project / Clip / Track / Transition

namespace XER::Bridge::FFmpeg {
struct AudioFrameInfo;   // fwd — full def lives in
                         // ffmpeg_engine_bridge.h; callers that fill
                         // the struct include that header themselves.
}  // namespace XER::Bridge::FFmpeg

namespace XER::VideoEditor {

struct AudioMixerOptions {
    // Empty -> DefaultDecoderFactory() (real FFmpeg bridge). Tests
    // inject fakes.
    MediaDecoderFactory decoder_factory;
    int sample_rate = 48000;    // output rate (export contract: 48 kHz)
    int channels = 2;           // output channels — v1 is stereo-only
    int chunk_samples = 1024;   // provider chunk size
};

// Timeline -> interleaved s16 stereo PCM chunk producer for the export
// mux. UI-free, single consumer thread. Init() snapshots the project;
// per-clip decoder sessions open lazily on the first chunk overlapping
// the clip's extended audio range ([timeline_start - in_handle,
// clip_end + out_handle) via ClipTransitionHandles — the same window
// the compositor uses) and close after it.
//
// A clip contributes audio iff: !clip.muted, its track exists,
// !(track.muted on an AUDIO-kind track — video-track `muted` is
// ignored per the model contract), NOT (video-kind track AND
// track.hidden — hidden removes video AND embedded audio), and the
// lazily-opened decoder reports HasAudio(). track.gain_db is a Phase-2
// field and is ignored here (clip.gain_db applies).
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    // Binds a snapshot of `project`. Fails only on invalid mixer
    // options (non-positive rate/chunk size, channels != 2) —
    // clip-level problems (missing files, no audio stream, undecodable
    // media) surface as Warnings() + silence or GetChunk errors, not
    // Init failures. `warnings_out` (optional) collects non-fatal
    // notes; Warnings() returns the accumulated list.
    bool Init(const Project& project, const AudioMixerOptions& options,
              std::string* error_out = nullptr,
              std::vector<std::string>* warnings_out = nullptr);

    void Shutdown();   // close all decoder sessions (idempotent)

    double DurationSeconds() const;   // project.duration()
    // ceil(duration * sample_rate / chunk_samples), >= 1 when
    // duration > 0 — the audio_frame_count for the encode calls.
    std::size_t ChunkCount() const;

    // Fills `out` for chunk `index`: media span
    // [index*chunk_samples/rate, min((index+1)*chunk/rate, duration)).
    // sample_rate/channels/planar/sample_format per the output
    // contract; sample_count = chunk_samples (last chunk short);
    // data = interleaved signed-16 LE L,R; pts = index*chunk_samples
    // (debug aid — the mux ignores it). Silence (all-zero PCM) where
    // no clip contributes. Returns false + error_out on null `out`,
    // out-of-range index ("chunk index out of range"), out-of-order
    // index, decode failure, or after Cancel() ("export cancelled").
    bool GetChunk(std::size_t index,
                  XER::Bridge::FFmpeg::AudioFrameInfo* out,
                  std::string* error_out = nullptr);

    void Cancel();          // cooperative; checked per GetChunk
    bool Cancelled() const;

    // Non-fatal issues seen so far (Init warnings + runtime notes like
    // "clip X has no audio stream"). Cleared by Init/Shutdown.
    const std::vector<std::string>& Warnings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::VideoEditor
