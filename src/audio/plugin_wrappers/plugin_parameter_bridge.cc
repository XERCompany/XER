#include "plugin_parameter_bridge.h"

#include <algorithm>

namespace XER::Engine::Audio::Plugin {

void PluginParameterBridge::Clear() {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    values_.clear();
}

void PluginParameterBridge::RegisterParameter(
    uint32_t id,
    const std::string& name,
    float default_value,
    float min_value,
    float max_value) {
    PluginParameterInfo info;
    info.id = id;
    info.name = name;
    info.default_value = default_value;
    info.current_value = default_value;
    info.min_value = min_value;
    info.max_value = max_value;
    RegisterParameter(info);
}

void PluginParameterBridge::RegisterParameter(const PluginParameterInfo& info) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    PluginParameterInfo normalized = info;
    float min_value = normalized.min_value;
    float max_value = normalized.max_value;
    if (min_value > max_value) {
        std::swap(min_value, max_value);
    }
    normalized.min_value = min_value;
    normalized.max_value = max_value;
    normalized.default_value = std::clamp(normalized.default_value, min_value, max_value);
    normalized.current_value = std::clamp(normalized.current_value, min_value, max_value);
    if (normalized.ui_hint.empty()) {
        normalized.ui_hint = min_value < max_value ? "slider" : "input";
    }
    values_[normalized.id] = normalized;
}

bool PluginParameterBridge::SetValue(uint32_t id, float value) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const auto it = values_.find(id);
    if (it == values_.end()) {
        return false;
    }
    it->second.current_value = std::clamp(value, it->second.min_value, it->second.max_value);
    return true;
}

bool PluginParameterBridge::SetNormalizedValue(uint32_t id, float normalized_value) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const auto it = values_.find(id);
    if (it == values_.end()) {
        return false;
    }
    const float clamped = std::clamp(normalized_value, 0.0f, 1.0f);
    it->second.current_value = it->second.min_value + (it->second.max_value - it->second.min_value) * clamped;
    return true;
}

bool PluginParameterBridge::GetValue(uint32_t id, float& value) const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const auto it = values_.find(id);
    if (it == values_.end()) {
        return false;
    }
    value = it->second.current_value;
    return true;
}

bool PluginParameterBridge::GetInfo(uint32_t id, PluginParameterInfo* info) const {
    if (info == nullptr) {
        return false;
    }
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    const auto it = values_.find(id);
    if (it == values_.end()) {
        return false;
    }
    *info = it->second;
    return true;
}

std::vector<PluginParameterInfo> PluginParameterBridge::Snapshot() const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    std::vector<PluginParameterInfo> snapshot;
    snapshot.reserve(values_.size());
    for (const auto& entry : values_) {
        snapshot.push_back(entry.second);
    }
    std::sort(snapshot.begin(), snapshot.end(), [](const PluginParameterInfo& lhs, const PluginParameterInfo& rhs) {
        return lhs.id < rhs.id;
    });
    return snapshot;
}

}  // namespace XER::Engine::Audio::Plugin
