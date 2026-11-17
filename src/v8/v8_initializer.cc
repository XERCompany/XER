#include "v8/v8_initializer.h"

#include <libplatform/libplatform.h>
#include <v8.h>

#include <memory>
#include <mutex>

namespace Engine::V8Runtime {
namespace {

std::mutex& V8Mutex() {
	static std::mutex mutex;
	return mutex;
}

bool& V8Initialized() {
	static bool initialized = false;
	return initialized;
}

std::unique_ptr<v8::Platform>& V8Platform() {
	static std::unique_ptr<v8::Platform> platform;
	return platform;
}

}  // namespace

void EnsureInitialized(int platform_workers) {
	std::lock_guard<std::mutex> guard(V8Mutex());
	if (V8Initialized()) {
		return;
	}
	v8::V8::InitializeICUDefaultLocation(nullptr);
	v8::V8::InitializeExternalStartupData(nullptr);
	V8Platform() = v8::platform::NewDefaultPlatform(platform_workers > 0 ? platform_workers : 0);
	v8::V8::InitializePlatform(V8Platform().get());
	v8::V8::Initialize();
	V8Initialized() = true;
}

void Shutdown() {
	std::lock_guard<std::mutex> guard(V8Mutex());
	if (!V8Initialized()) {
		return;
	}
	v8::V8::Dispose();
	v8::V8::DisposePlatform();
	V8Platform().reset();
	V8Initialized() = false;
}

}  // namespace Engine::V8Runtime
