#include "audio_engine_host.h"
#include "audio_engine.h"

namespace XER::Audio::Core {

AudioEngineHost::AudioEngineHost()
    : global_sample_rate_(44100), global_buffer_size_(512) {}

AudioEngineHost::~AudioEngineHost() {
    StopAllEngines();
    engines_.clear();
}

bool AudioEngineHost::CreateEngine(const std::string& engine_id, int sample_rate, int buffer_size, int channels) {
    // Check if engine already exists
    for (const auto& pair : engines_) {
        if (pair.first == engine_id) {
            return false;  // Engine already exists
        }
    }

    auto engine = std::make_shared<AudioEngine>();
    if (!engine->Initialize(sample_rate, buffer_size, channels)) {
        return false;
    }

    engines_.push_back({engine_id, engine});
    return true;
}

bool AudioEngineHost::DestroyEngine(const std::string& engine_id) {
    for (auto it = engines_.begin(); it != engines_.end(); ++it) {
        if (it->first == engine_id) {
            it->second->Stop();
            engines_.erase(it);
            return true;
        }
    }
    return false;
}

std::shared_ptr<AudioEngine> AudioEngineHost::GetEngine(const std::string& engine_id) {
    for (const auto& pair : engines_) {
        if (pair.first == engine_id) {
            return pair.second;
        }
    }
    return nullptr;
}

bool AudioEngineHost::StartAllEngines() {
    for (auto& pair : engines_) {
        if (!pair.second->Start()) {
            return false;
        }
    }
    return true;
}

bool AudioEngineHost::StopAllEngines() {
    for (auto& pair : engines_) {
        pair.second->Stop();
    }
    return true;
}

std::vector<std::string> AudioEngineHost::GetEngineList() const {
    std::vector<std::string> engine_ids;
    for (const auto& pair : engines_) {
        engine_ids.push_back(pair.first);
    }
    return engine_ids;
}

bool AudioEngineHost::SetGlobalSampleRate(int sample_rate) {
    global_sample_rate_ = sample_rate;
    for (auto& pair : engines_) {
        pair.second->SetSampleRate(sample_rate);
    }
    return true;
}

bool AudioEngineHost::SetGlobalBufferSize(int buffer_size) {
    global_buffer_size_ = buffer_size;
    for (auto& pair : engines_) {
        pair.second->SetBufferSize(buffer_size);
    }
    return true;
}

int AudioEngineHost::GetEngineCount() const {
    return static_cast<int>(engines_.size());
}

std::string AudioEngineHost::GetHostInfo() const {
    std::string info = "AudioEngineHost: ";
    info += std::to_string(engines_.size()) + " engines, ";
    info += std::to_string(global_sample_rate_) + "Hz";
    return info;
}

}  // namespace XER::Audio::Core
