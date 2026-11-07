#pragma once

#include <memory>
#include <vector>
#include <string>

namespace XER::Engine::Audio::Core {

class AudioEngine;

class AudioEngineHost {
public:
    AudioEngineHost();
    ~AudioEngineHost();

    // Engine lifecycle
    bool CreateEngine(const std::string& engine_id, int sample_rate, int buffer_size, int channels);
    bool DestroyEngine(const std::string& engine_id);
    std::shared_ptr<AudioEngine> GetEngine(const std::string& engine_id);

    // Multi-engine management
    bool StartAllEngines();
    bool StopAllEngines();
    std::vector<std::string> GetEngineList() const;

    // Global settings
    bool SetGlobalSampleRate(int sample_rate);
    bool SetGlobalBufferSize(int buffer_size);
    
    // Info
    int GetEngineCount() const;
    std::string GetHostInfo() const;

private:
    std::vector<std::pair<std::string, std::shared_ptr<AudioEngine>>> engines_;
    int global_sample_rate_;
    int global_buffer_size_;
};

}  // namespace XER::Engine::Audio::Core
