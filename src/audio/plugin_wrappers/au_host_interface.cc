#include "au_host_interface.h"
#include "helper/string_utils.h"

#include <algorithm>
#include <cctype>

namespace {

std::string NormalizeBuiltinReference(std::string reference) {
    reference = XER::Helper::String::ToLowerAscii(reference);
    constexpr const char* kBuiltinPrefix = "builtin://";
    if (reference.rfind(kBuiltinPrefix, 0) == 0) {
        reference.erase(0, std::char_traits<char>::length(kBuiltinPrefix));
    }
    constexpr const char* kBuiltinShortPrefix = "builtin:";
    if (reference.rfind(kBuiltinShortPrefix, 0) == 0) {
        reference.erase(0, std::char_traits<char>::length(kBuiltinShortPrefix));
    }
    return reference;
}

bool IsBuiltinComponentReference(const std::string& reference) {
    const std::string normalized = NormalizeBuiltinReference(reference);
    return normalized == "passthrough"
        || normalized == "gain"
        || normalized == "compressor"
        || normalized == "chorus"
        || normalized == "parametric_eq"
        || normalized == "eq";
}

}  // namespace

namespace XER::Audio::Plugin {

bool AuHostInterface::Initialize(double sample_rate, uint32_t max_block_size) {
    if (sample_rate <= 0.0 || max_block_size == 0) {
        return false;
    }
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    sample_rate_ = sample_rate;
    max_block_size_ = max_block_size;
	component_id_.clear();
	backend_mode_ = BackendMode::kNone;
    return builtin_host_.Initialize(sample_rate, max_block_size);
}

bool AuHostInterface::LoadComponent(const std::string& component_id) {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    if (!builtin_host_.Load(component_id)) {
		backend_mode_ = IsBuiltinComponentReference(component_id) ? BackendMode::kNone : BackendMode::kUnsupportedExternal;
		component_id_ = backend_mode_ == BackendMode::kUnsupportedExternal
			? "unsupported://audio-unit-unavailable-on-linux"
			: std::string();
        return false;
    }
	backend_mode_ = BackendMode::kBuiltin;
    component_id_ = builtin_host_.GetLoadedIdentifier();
    return true;
}

bool AuHostInterface::Process(const float* input, float* output, uint32_t frames) {
    if (input == nullptr || output == nullptr || frames == 0 || frames > max_block_size_) {
        return false;
    }
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    if (backend_mode_ != BackendMode::kBuiltin) {
        return false;
    }
    return builtin_host_.Process(input, output, frames);
}

bool AuHostInterface::SetParameter(uint32_t id, float value) {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    if (backend_mode_ != BackendMode::kBuiltin) {
        return false;
    }
	return builtin_host_.SetParameter(id, value);
}

bool AuHostInterface::GetParameter(uint32_t id, float* value) const {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    if (backend_mode_ != BackendMode::kBuiltin) {
        return false;
    }
	return builtin_host_.GetParameter(id, value);
}

std::vector<PluginParameterInfo> AuHostInterface::GetParameters() const {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    if (backend_mode_ != BackendMode::kBuiltin) {
        return {};
    }
	return builtin_host_.GetParameters();
}

std::string AuHostInterface::GetLoadedComponentId() const {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	return component_id_;
}

std::string AuHostInterface::GetBackendMode() const {
    XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
    switch (backend_mode_) {
    case BackendMode::kBuiltin:
        return "builtin";
    case BackendMode::kUnsupportedExternal:
        return "unsupported_external";
    case BackendMode::kNone:
        return "none";
    }
    return "none";
}

}  // namespace XER::Audio::Plugin
