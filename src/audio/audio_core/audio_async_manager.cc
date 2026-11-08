/**
 * This file is part of XER, Audio subsystem.
 * Copyright (C) 2026 Yoshi A.
 */
#include "audio/audio_core/audio_async_manager.h"

#include "async_io/io_thread_pool.h"

namespace XER {
namespace Audio {
namespace Core {

class AudioAsyncManager::Impl {
public:
    Impl() : thread_pool_(XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance()) {}

    XER::AsyncIO::IO::Async::IOThreadPool& thread_pool_;
};

AudioAsyncManager::AudioAsyncManager() : impl_(std::make_unique<Impl>()) {}

AudioAsyncManager::~AudioAsyncManager() = default;

AudioAsyncManager& AudioAsyncManager::Instance() {
    static AudioAsyncManager instance;
    return instance;
}

void AudioAsyncManager::EnqueueAudioOperation(std::function<void()> operation) {
    impl_->thread_pool_.Enqueue(std::move(operation));
}

std::future<void> AudioAsyncManager::ProcessAudioAsync(float* input, float* output, size_t frames) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    EnqueueAudioOperation([promise, input, output, frames]() {
        // TODO: Implement actual audio processing
        // For now, just copy input to output
        if (input && output) {
            std::copy(input, input + frames, output);
        }
        promise->set_value();
    });

    return future;
}

std::future<AudioAsyncManager::AudioSourceBuffer>
AudioAsyncManager::LoadAudioFileAsync(const std::string& path) {
    auto promise = std::make_shared<std::promise<AudioSourceBuffer>>();
    auto future = promise->get_future();

    EnqueueAudioOperation([promise, path]() {
        // TODO: Implement actual file loading
        AudioSourceBuffer buffer;
        buffer.sample_rate = 48000;
        buffer.channels = 2;
        buffer.frame_count = 0;
        promise->set_value(buffer);
    });

    return future;
}

std::future<AudioAsyncManager::AnalysisResult>
AudioAsyncManager::AnalyzeAudioAsync(const float* audio, size_t frames) {
    auto promise = std::make_shared<std::promise<AnalysisResult>>();
    auto future = promise->get_future();

    EnqueueAudioOperation([promise, audio, frames]() {
        // TODO: Implement actual analysis
        AnalysisResult result;
        promise->set_value(result);
    });

    return future;
}

std::future<void> AudioAsyncManager::ProcessEffectAsync(const std::string& effect_name,
                                                         float* input, float* output,
                                                         size_t frames) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    EnqueueAudioOperation([promise, effect_name, input, output, frames]() {
        // TODO: Implement actual effect processing
        if (input && output) {
            std::copy(input, input + frames, output);
        }
        promise->set_value();
    });

    return future;
}

void AudioAsyncManager::Start() {
    if (!impl_->thread_pool_.IsRunning()) {
        impl_->thread_pool_.Start();
    }
}

void AudioAsyncManager::Stop() {
    if (impl_->thread_pool_.IsRunning()) {
        impl_->thread_pool_.Stop();
    }
}

bool AudioAsyncManager::IsRunning() const {
    return impl_->thread_pool_.IsRunning();
}

size_t AudioAsyncManager::GetPendingOperationCount() const {
    return impl_->thread_pool_.GetPendingTaskCount();
}

}  // namespace Core
}  // namespace Audio
}  // namespace XER