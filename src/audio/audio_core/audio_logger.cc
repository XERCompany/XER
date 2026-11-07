/**
 * This file is part of XER, Audio subsystem.
 * Copyright (C) 2026 Yoshi A.
 */
#include "audio/audio_core/audio_logger.h"

#include "flux/core/logger.h"

namespace XER::Engine {
namespace Audio {
namespace Core {

class AudioLogger::Impl {
public:
    XER::flux::core::Logger logger;
};

AudioLogger::AudioLogger() : impl_(std::make_unique<Impl>()) {}

AudioLogger& AudioLogger::Instance() {
    static AudioLogger instance;
    return instance;
}

void AudioLogger::LogEngineStart(int sample_rate, int buffer_size) {
    impl_->logger.Info("audio",
                      "AudioEngine started sample_rate=" + std::to_string(sample_rate) +
                      " buffer_size=" + std::to_string(buffer_size));
}

void AudioLogger::LogEngineStop() {
    impl_->logger.Info("audio", "AudioEngine stopped");
}

void AudioLogger::LogEngineError(const std::string& error) {
    impl_->logger.Error("audio", error);
}

void AudioLogger::LogEffectLoad(const std::string& effect_name) {
    impl_->logger.Info("audio", "Effect loaded: " + effect_name);
}

void AudioLogger::LogEffectProcess(const std::string& effect_name, double cpu_time) {
    impl_->logger.Debug("audio",
                       effect_name + " cpu_time=" + std::to_string(cpu_time) + "ms");
}

void AudioLogger::LogEffectError(const std::string& effect_name, const std::string& error) {
    impl_->logger.Error("audio", effect_name + ": " + error);
}

void AudioLogger::LogAnalysisStart(const std::string& analysis_type) {
    impl_->logger.Info("audio", "Analysis started: " + analysis_type);
}

void AudioLogger::LogAnalysisComplete(const std::string& analysis_type, double duration) {
    impl_->logger.Info("audio",
                      analysis_type + " duration=" + std::to_string(duration) + "ms");
}

void AudioLogger::LogAnalysisError(const std::string& analysis_type, const std::string& error) {
    impl_->logger.Error("audio", analysis_type + ": " + error);
}

void AudioLogger::LogFileLoad(const std::string& path, size_t size_ms) {
    impl_->logger.Info("audio", path + " size=" + std::to_string(size_ms) + "ms");
}

void AudioLogger::LogFileSave(const std::string& path, size_t size_ms) {
    impl_->logger.Info("audio", "File saved: " + path + " size=" + std::to_string(size_ms) + "ms");
}

void AudioLogger::LogFileError(const std::string& path, const std::string& error) {
    impl_->logger.Error("audio", path + ": " + error);
}

void AudioLogger::LogBufferAllocation(size_t size) {
    impl_->logger.Debug("audio", "Buffer allocated size=" + std::to_string(size) + " bytes");
}

void AudioLogger::LogBufferDeallocation(size_t size) {
    impl_->logger.Debug("audio", "Buffer deallocated size=" + std::to_string(size) + " bytes");
}

void AudioLogger::LogFFTOperation(size_t fft_size, double duration) {
    impl_->logger.Debug("audio",
                      "FFT size=" + std::to_string(fft_size) +
                      " duration=" + std::to_string(duration) + "ms");
}

void AudioLogger::LogFilterOperation(const std::string& filter_type, double duration) {
    impl_->logger.Debug("audio",
                      filter_type + " duration=" + std::to_string(duration) + "ms");
}

}  // namespace Core
}  // namespace Audio
}  // namespace XER::Engine