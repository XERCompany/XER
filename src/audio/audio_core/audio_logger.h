/**
 * This file is part of XER, Audio subsystem.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <memory>
#include <string>

namespace XER::Engine {
namespace Audio {
namespace Core {

class AudioLogger {
public:
    static AudioLogger& Instance();

    // Audio engine events
    void LogEngineStart(int sample_rate, int buffer_size);
    void LogEngineStop();
    void LogEngineError(const std::string& error);

    // Effect events
    void LogEffectLoad(const std::string& effect_name);
    void LogEffectProcess(const std::string& effect_name, double cpu_time);
    void LogEffectError(const std::string& effect_name, const std::string& error);

    // Analysis events
    void LogAnalysisStart(const std::string& analysis_type);
    void LogAnalysisComplete(const std::string& analysis_type, double duration);
    void LogAnalysisError(const std::string& analysis_type, const std::string& error);

    // File I/O events
    void LogFileLoad(const std::string& path, size_t size_ms);
    void LogFileSave(const std::string& path, size_t size_ms);
    void LogFileError(const std::string& path, const std::string& error);

    // Buffer events
    void LogBufferAllocation(size_t size);
    void LogBufferDeallocation(size_t size);

    // DSP events
    void LogFFTOperation(size_t fft_size, double duration);
    void LogFilterOperation(const std::string& filter_type, double duration);

private:
    AudioLogger();
    ~AudioLogger() = default;

    AudioLogger(const AudioLogger&) = delete;
    AudioLogger& operator=(const AudioLogger&) = delete;

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Core
}  // namespace Audio
}  // namespace XER::Engine