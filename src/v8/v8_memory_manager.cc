#include "v8/v8_memory_manager.h"

#include <string>

#include "resource_guard.h"

namespace XER::V8Runtime {

v8::Isolate::CreateParams BuildCreateParams() {
	v8::Isolate::CreateParams create_params;
	create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
	return create_params;
}

void ApplyMemorySettings(const EngineParams& params, std::string* warning_out) {
	if (params.max_memory_used > 0) {
		const std::string flag = "--max-old-space-size=" + std::to_string(params.max_memory_used);
		v8::V8::SetFlagsFromString(flag.c_str(), static_cast<int>(flag.size()));
	}
	const int hard_mib = params.memory_hard_limit_mib > 0
		? params.memory_hard_limit_mib
		: params.max_memory_used;
	ApplyProcessMemoryLimits(hard_mib, warning_out);
}

void DestroyAllocator(v8::Isolate* isolate) {
	if (isolate == nullptr) {
		return;
	}
	auto* allocator = isolate->GetArrayBufferAllocator();
	delete allocator;
}

}  // namespace XER::V8Runtime
