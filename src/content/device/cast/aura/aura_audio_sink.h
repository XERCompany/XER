/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 *
 * AuraAudioSink — bridges Auracast BLE Audio frames into the XER audio pipeline.
 *
 * Data flow:
 *
 *   BT_ISO read()
 *       │
 *       ▼  AudioFrameCallback (raw LC3 bytes per BIS)
 *   AuraLc3Decoder::Decode()          ← aura_lc3_decoder.h
 *       │  Lc3DecodedFrame (planar float32)
 *       ▼
 *   AudioSampleRateConverter          ← audio/audio_core/audio_sample_rate_converter.h
 *       │  resampled to target_rate_ (default 48 kHz → AudioEngine rate)
 *       ▼
 *   AudioInterleaveProcessor::Interleave()  ← audio/audio_core/audio_interleave_processor.h
 *       │  interleaved float32
 *       ▼
 *   AudioFIFOQueue::Write()            ← audio/audio_core/audio_fifo_queue.h
 *       │
 *       └──► AudioEngine::ProcessAudio()  ← audio/audio_core/audio_engine.h
 *                 │
 *                 ▼  AudioProcessCallback → speakers / recording / effects
 *
 * AuraAudioSink is set as the AudioFrameCallback of AuraCastSession.
 * It is also the AudioProcessCallback source for AudioEngine.
 *
 * Multi-BIS stereo:
 *   Auracast stereo sources expose two BIS (BIS index 0 = left, 1 = right).
 *   The sink accumulates both into separate Lc3DecodedFrame buffers and merges
 *   them into one stereo FIFO write per pair.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "content/device/cast/aura/aura_lc3_decoder.h"
#include "audio/audio_core/audio_engine.h"
#include "audio/audio_core/audio_buffer_manager.h"
#include "audio/audio_core/audio_fifo_queue.h"
#include "audio/audio_core/audio_sample_rate_converter.h"
#include "audio/audio_core/audio_interleave_processor.h"

namespace device::cast::aura {

// Metrics gathered by the sink.
struct AuraAudioSinkStats {
    uint64_t frames_received    = 0;  // LC3 frames received from BLE
    uint64_t frames_decoded     = 0;  // successfully decoded
    uint64_t frames_dropped     = 0;  // FIFO overflow
    uint64_t decode_errors      = 0;
    double   fifo_fill_ratio    = 0.0; // 0=empty … 1=full
    double   latency_ms         = 0.0; // estimated end-to-end latency
};

// AuraAudioSink receives raw LC3 BIS frames, decodes and feeds AudioEngine.
class AuraAudioSink {
public:
    // |channel_count|: 1 (mono BIS) or 2 (stereo, two BIS).
    explicit AuraAudioSink(uint8_t channel_count = 2);
    ~AuraAudioSink();

    // ── Setup ────────────────────────────────────────────────────────────────

    // Configure LC3 decoder and audio pipeline.
    // Must be called before Start().  |params| come from AuraCastDevice.
    bool Initialize(const Lc3Params& lc3_params,
                    int target_sample_rate = 48000);

    // Attach to (or create) an AudioEngine instance.
    // If |engine| is null, a new engine is created internally.
    bool AttachEngine(XER::Audio::Core::AudioEngine* engine = nullptr);

    // ── Lifecycle ────────────────────────────────────────────────────────────

    bool Start();  // enable FIFO intake and AudioEngine
    void Stop();
    bool IsRunning() const { return running_.load(); }

    // ── AudioFrameCallback compatible entry point ─────────────────────────

    // Called by AuraCastSession for each received LC3 frame.
    // Thread-safe; can be called from the IOThreadPool rx thread.
    void OnBisFrame(uint8_t bis_index, const uint8_t* data, size_t len);

    // Convenience: return a std::function wrapping OnBisFrame.
    std::function<void(uint8_t, const uint8_t*, size_t)> GetFrameCallback() {
        return [this](uint8_t b, const uint8_t* d, size_t l) {
            OnBisFrame(b, d, l);
        };
    }

    // ── Statistics ───────────────────────────────────────────────────────────
    AuraAudioSinkStats GetStats() const;

    // ── Audio output volume ───────────────────────────────────────────────
    void SetVolume(float gain) { volume_.store(gain); }  // 0.0–1.0
    float GetVolume() const    { return volume_.load(); }

    // ── Direct FIFO read (for custom AudioProcessCallback) ────────────────
    // Reads |frame_count| interleaved stereo frames from the output FIFO.
    size_t ReadPcm(float* out, size_t frame_count);

    // XER::Audio::Core::AudioProcessCallback compatible.
    // Attach with engine->SetProcessCallback(...).
    void AudioEngineCallback(float** /*inputs*/, float** outputs,
                              int channels, size_t frames);

private:
    uint8_t channel_count_;
    int     target_rate_ = 48000;
    Lc3Params lc3_params_;

    AuraLc3Decoder decoder_;

    std::unique_ptr<XER::Audio::Core::AudioFIFOQueue>          fifo_;
    std::unique_ptr<XER::Audio::Core::AudioSampleRateConverter> src_;
    std::unique_ptr<XER::Audio::Core::AudioBufferManager>       buf_mgr_;
    XER::Audio::Core::AudioEngine* engine_     = nullptr;
    bool                               owns_engine_ = false;

    std::atomic<bool>  running_{ false };
    std::atomic<float> volume_{ 1.f };

    // Stereo accumulation: hold the last decoded left frame until right arrives.
    std::vector<float> pending_left_;
    bool               pending_left_valid_ = false;

    // Stats
    mutable std::atomic<uint64_t> stat_received_{ 0 };
    mutable std::atomic<uint64_t> stat_decoded_{ 0 };
    mutable std::atomic<uint64_t> stat_dropped_{ 0 };
    mutable std::atomic<uint64_t> stat_errors_{ 0 };
};

}  // namespace device::cast::aura
