#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "async_io/sync_primitives/mutex_wrapper.h"

namespace XER::Audio::Plugin {

struct PluginParameterInfo {
	uint32_t id = 0;
	std::string name;
    std::string module;
    std::string unit;
    std::string ui_hint;
    std::string description;
    uint32_t flags = 0;
    bool is_stepped = false;
    bool is_automatable = false;
	float min_value = 0.0f;
	float max_value = 1.0f;
	float default_value = 0.0f;
	float current_value = 0.0f;
};

class PluginParameterBridge {
public:
    void Clear();
    void RegisterParameter(uint32_t id, const std::string& name, float default_value, float min_value, float max_value);
    void RegisterParameter(const PluginParameterInfo& info);
    bool SetValue(uint32_t id, float value);
    bool SetNormalizedValue(uint32_t id, float normalized_value);
    bool GetValue(uint32_t id, float& value) const;
    bool GetInfo(uint32_t id, PluginParameterInfo* info) const;
    std::vector<PluginParameterInfo> Snapshot() const;

private:
    mutable XER::AsyncIO::IO::Sync::MutexWrapper mutex_{"plugin_param_bridge"};
    std::unordered_map<uint32_t, PluginParameterInfo> values_;
};

}  // namespace XER::Audio::Plugin
