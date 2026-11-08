/**
 * This file is part of XER, Device open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/device/cast/aura/aura_audio_sink.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace device::cast::aura {

// FIFO capacity in PCM frames; ~2 s at 48 kHz.
static constexpr size_t kFifoFrames = 48000 * 2;

// ──────────────────────────────────────────────────────────────────────────────

AuraAudioSink::AuraAudioSink(uint8_t channel_count)
    : channel_count_(channel_count) {}

AuraAudioSink::~AuraAudioSink() {
    Stop();
    if (owns_engine_ && engine_) {
        engine_->Stop();
        delete engine_;
    }
}

// ──────────────────────────────────────────────────────────────────────────────

bool AuraAudioSink::Initialize(const Lc3Params& params, int target_rate) {
    lc3_params_  = params;
    target_rate_ = target_rate;

    if (!decoder_.Initialize(params)) return false;

    // FIFO: stores interleaved frames (channel_count_ samples per frame).
    fifo_ = std::make_unique<XER::Audio::Core::AudioFIFOQueue>(
        kFifoFrames * channel_count_);

    // Sample-rate converter: LC3 source rate → AudioEngine rate.
    src_ = std::make_unique<XER::Audio::Core::AudioSampleRateConverter>(
        XER::Audio::Core::ResampleQuality::HIGH);
    if (static_cast<int>(params.sampling_freq_hz) != target_rate) {
        src_->Initialize(static_cast<int>(params.sampling_freq_hz), target_rate);
    }

    // Buffer manager for temporary conversion buffers.
    buf_mgr_ = std::make_unique<XER::Audio::Core::AudioBufferManager>(4);

    return true;
}

bool AuraAudioSink::AttachEngine(XER::Audio::Core::AudioEngine* engine) {
    if (engine) {
        engine_      = engine;
        owns_engine_ = false;
    } else {
        engine_      = new XER::Audio::Core::AudioEngine();
        owns_engine_ = true;
    }

    // Wire up the engine's process callback to pull from our FIFO.
    engine_->SetProcessCallback(
        [this](float** inputs, float** outputs, int channels, size_t frames) {
            AudioEngineCallback(inputs, outputs, channels, frames);
        });

    return true;
}

bool AuraAudioSink::Start() {
    if (running_.load()) return true;
    if (!fifo_) return false;

    if (engine_ && !engine_->IsRunning()) {
        // Initialize engine if not yet done (48 kHz, 512-frame buffer).
        if (!engine_->IsInitialized()) {
            engine_->Initialize(target_rate_, 512, channel_count_);
        }
        engine_->Start();
    }

    running_.store(true);
    return true;
}

void AuraAudioSink::Stop() {
    running_.store(false);
    if (engine_ && engine_->IsRunning()) engine_->Stop();
}

// ──────────────────────────────────────────────────────────────────────────────
// OnBisFrame — hot path called from IOThreadPool receive thread.
// ──────────────────────────────────────────────────────────────────────────────

void AuraAudioSink::OnBisFrame(uint8_t bis_index,
                                const uint8_t* data, size_t len) {
    if (!running_.load()) return;
    stat_received_.fetch_add(1, std::memory_order_relaxed);

    // Decode the LC3 frame into planar float32.
    auto decoded = decoder_.Decode(bis_index, data, len);
    if (decoded.frame_count == 0) {
        stat_errors_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    stat_decoded_.fetch_add(1, std::memory_order_relaxed);

    std::vector<float> pcm_out;
    const float* src_ptr = nullptr;
    size_t pcm_frames = decoded.frame_count;

    // ── Resampling (if source rate ≠ target rate) ───────────────────────
    if (src_ && src_->IsInitialized()) {
        const size_t max_out = static_cast<size_t>(
            static_cast<double>(pcm_frames) * src_->GetRatio() + 64);
        pcm_out.resize(max_out);
        size_t out_frames = max_out;
        src_->Convert(decoded.planar[0].data(), pcm_frames,
                       pcm_out.data(), out_frames);
        pcm_out.resize(out_frames);
        pcm_frames = out_frames;
        src_ptr    = pcm_out.data();
    } else {
        src_ptr = decoded.planar[0].data();
    }

    // ── Volume ───────────────────────────────────────────────────────────
    const float gain = volume_.load();
    if (gain != 1.f && src_ptr) {
        if (pcm_out.empty()) {
            pcm_out.assign(src_ptr, src_ptr + pcm_frames);
            src_ptr = pcm_out.data();
        }
        for (size_t i = 0; i < pcm_frames; ++i)
            pcm_out[i] *= gain;
    }

    // ── Stereo interleaving ───────────────────────────────────────────────
    if (channel_count_ == 2) {
        if (bis_index == 0) {
            // Left channel — store and wait for right.
            pending_left_.assign(src_ptr, src_ptr + pcm_frames);
            pending_left_valid_ = true;
            return;
        }
        if (bis_index == 1 && pending_left_valid_) {
            // Right arrived — interleave and push stereo to FIFO.
            const size_t n = std::min(pending_left_.size(), pcm_frames);
            const float* planar[2] = { pending_left_.data(), src_ptr };
            std::vector<float> interleaved(n * 2);
            XER::Audio::Core::AudioInterleaveProcessor::Interleave(
                planar, 2, n, interleaved.data());

            const size_t written = fifo_->Write(interleaved.data(), n * 2);
            if (written < n * 2)
                stat_dropped_.fetch_add(1, std::memory_order_relaxed);
            pending_left_valid_ = false;
            return;
        }
    }

    // ── Mono path ────────────────────────────────────────────────────────
    const size_t written = fifo_->Write(src_ptr, pcm_frames);
    if (written < pcm_frames)
        stat_dropped_.fetch_add(1, std::memory_order_relaxed);
}

// ──────────────────────────────────────────────────────────────────────────────

size_t AuraAudioSink::ReadPcm(float* out, size_t frame_count) {
    if (!fifo_) return 0;
    return fifo_->Read(out, frame_count * channel_count_) / channel_count_;
}

void AuraAudioSink::AudioEngineCallback(float** /*inputs*/,
                                         float** outputs,
                                         int channels, size_t frames) {
    if (!fifo_ || !outputs) return;
    const int ch = std::min(channels, static_cast<int>(channel_count_));
    // Read interleaved from FIFO, then deinterleave into engine output planes.
    std::vector<float> interleaved(frames * static_cast<size_t>(ch), 0.f);
    fifo_->Read(interleaved.data(), interleaved.size());

    float* planar[8] = {};
    for (int i = 0; i < ch; ++i) planar[i] = outputs[i];
    XER::Audio::Core::AudioInterleaveProcessor::Deinterleave(
        interleaved.data(), ch,
        frames, planar);
}

AuraAudioSinkStats AuraAudioSink::GetStats() const {
    AuraAudioSinkStats s;
    s.frames_received = stat_received_.load(std::memory_order_relaxed);
    s.frames_decoded  = stat_decoded_.load(std::memory_order_relaxed);
    s.frames_dropped  = stat_dropped_.load(std::memory_order_relaxed);
    s.decode_errors   = stat_errors_.load(std::memory_order_relaxed);
    if (fifo_) {
        const size_t avail = fifo_->GetAvailableFrames();
        const size_t cap   = fifo_->GetCapacity();
        s.fifo_fill_ratio  = cap > 0 ? static_cast<double>(avail) / cap : 0.0;
        // Estimated latency: FIFO fill / (channel_count * sample_rate).
        s.latency_ms = cap > 0
            ? (avail / static_cast<double>(channel_count_)) /
              lc3_params_.sampling_freq_hz * 1000.0
            : 0.0;
    }
    return s;
}

}  // namespace device::cast::aura
