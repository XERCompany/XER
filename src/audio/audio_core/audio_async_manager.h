/**
 * This file is part of XER, Audio subsystem.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace XER::Engine {
namespace Audio {
namespace Core {

class AudioAsyncManager {
public:
    static AudioAsyncManager& Instance();

    // Enqueue an audio operation to the thread pool
    void EnqueueAudioOperation(std::function<void()> operation);

    // Async audio processing
    std::future<void> ProcessAudioAsync(float* input, float* output, size_t frames);

    // Async file loading
    struct AudioSourceBuffer {
        std::vector<float> data;
        int sample_rate;
        int channels;
        size_t frame_count;
    };
    std::future<AudioSourceBuffer> LoadAudioFileAsync(const std::string& path);

    // Async analysis
    struct AnalysisResult {
        std::vector<float> data;
        std::string metadata;
    };
    std::future<AnalysisResult> AnalyzeAudioAsync(const float* audio, size_t frames);

    // Async effect processing
    std::future<void> ProcessEffectAsync(const std::string& effect_name,
                                         float* input, float* output,
                                         size_t frames);

    // Thread pool control
    void Start();
    void Stop();
    bool IsRunning() const;

    // Statistics
    size_t GetPendingOperationCount() const;

private:
    AudioAsyncManager();
    ~AudioAsyncManager();

    AudioAsyncManager(const AudioAsyncManager&) = delete;
    AudioAsyncManager& operator=(const AudioAsyncManager&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Core
}  // namespace Audio
}  // namespace XER::Engine