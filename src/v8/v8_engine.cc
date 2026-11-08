#include "v8/v8_engine.h"

#include <string>

#include "v8/v8_initializer.h"
#include "v8/v8_memory_manager.h"

namespace XER::V8Runtime {

v8::Isolate* CreateManagedIsolate(const EngineParams& params, std::string* warning_out) {
	EnsureInitialized(params.v8_platform_workers);
	ApplyMemorySettings(params, warning_out);
	v8::Isolate::CreateParams create_params = BuildCreateParams();
	return v8::Isolate::New(create_params);
}

void DisposeManagedIsolate(v8::Isolate* isolate) {
	if (isolate == nullptr) {
		return;
	}
	DestroyAllocator(isolate);
	isolate->Dispose();
}

}  // namespace XER::V8Runtime
