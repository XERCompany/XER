/**
 * Callback-driven sink implementations: hosts wire arbitrary callbacks
 * into the sink contracts (used by tests, remote-render hosts and the
 * in-process player path).
 */
#pragma once

#include "media/sinks/audio_renderer_sink.h"
#include "media/sinks/video_renderer_sink.h"

namespace XER::Media {

class CallbackAudioSink final : public AudioRendererSink {
public:
    using VolumeCallback = std::function<void(double)>;

    void SetPull(Callback cb) { pull_ = std::move(cb); }
    void SetVolumeCallback(VolumeCallback cb) { volume_cb_ = std::move(cb); }

    void Start(Callback cb) override { pull_ = std::move(cb); running_ = true; }
    void Stop() override { running_ = false; }
    void Play() override { playing_ = true; }
    void Pause() override { playing_ = false; }
    void SetVolume(double volume) override {
        volume_ = volume;
        if (volume_cb_) volume_cb_(volume);
    }

    // Host-side tick: invokes the registered pull when running+playing.
    void Tick() {
        if (running_ && playing_ && pull_) pull_();
    }
    double volume() const { return volume_; }

private:
    Callback pull_;
    VolumeCallback volume_cb_;
    bool running_ = false;
    bool playing_ = false;
    double volume_ = 1.0;
};

class CallbackVideoSink final : public VideoRendererSink {
public:
    void SetPull(Callback cb) { pull_ = std::move(cb); }
    void Start(Callback cb) override { pull_ = std::move(cb); running_ = true; }
    void Stop() override { running_ = false; }

    void Tick() {
        if (running_ && pull_) pull_();
    }

private:
    Callback pull_;
    bool running_ = false;
};

}  // namespace XER::Media
