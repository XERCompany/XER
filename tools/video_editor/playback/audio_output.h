// Audio output abstraction for the VideoEditor preview engine
// (Phase 2). The preview worker feeds an IAudioSink with AudioMixer
// output chunks; the UI supplies a real device sink (WASAPI shared-
// mode render — the natural implementation mirrors
// src/content/video/video_audio_player.*, which is too coupled to
// VideoSession to reuse directly) while tests use a counting sink.
// Header-only interface — no implementation lives in the core.
#pragma once

#include <cstdint>

namespace XER::VideoEditor {

// Push-model PCM destination. Stream contract is fixed by AudioMixer:
// interleaved signed-16 little-endian STEREO at the engine's
// configured sample rate (default 48000). `frame_count` is the number
// of stereo frames; `interleaved_samples` holds frame_count*2 s16
// values valid only for the duration of the call.
//
// Write() is only ever invoked on the engine's worker thread. An
// implementation MAY block inside Write to pace the pump against the
// output device (a WASAPI sink throttles naturally at its buffer
// depth — that gives free A/V sync); it must not call back into the
// engine. Return false to report a sink-side failure: the chunk is
// dropped, a one-time warning is recorded, and video playback
// continues — audio is best-effort like Premiere (it sheds audio
// before it sheds frames). The sink must outlive the engine (or be
// detached by Shutdown() before destruction).
class IAudioSink {
public:
    virtual ~IAudioSink() = default;
    virtual bool Write(const std::int16_t* interleaved_samples,
                       int frame_count, int sample_rate) = 0;
};

}  // namespace XER::VideoEditor
