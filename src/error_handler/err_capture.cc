#include "error_handler/err_capture.h"

#include <mutex>
#include <string>

#include <absl/strings/str_cat.h>
#include <range/v3/algorithm/any_of.hpp>

#include "crash/crash_handler.h"
#include "helper/string.h"

namespace XER::ErrorHandler {
namespace {

std::mutex& CaptureMutex() {
	static std::mutex mutex;
	return mutex;
}

CaptureConfiguration& MutableCaptureConfiguration() {
	static CaptureConfiguration configuration;
	return configuration;
}

}  // namespace

void InitializeCrashCapture(const CaptureConfiguration& config) {
	std::lock_guard<std::mutex> lock(CaptureMutex());
	MutableCaptureConfiguration().script_path = Helper::String::NormalizeUtf8(config.script_path);
	MutableCaptureConfiguration().dump_dir = Helper::String::DefaultString(config.dump_dir, ".");
	CrashHandler::Install();
	if (!MutableCaptureConfiguration().script_path.empty()) {
		CrashHandler::SetScriptPath(MutableCaptureConfiguration().script_path);
	}
	CrashHandler::SetDumpDir(MutableCaptureConfiguration().dump_dir);
}

void UpdateCrashContext(std::string_view script_path, std::string_view dump_dir) {
	std::lock_guard<std::mutex> lock(CaptureMutex());
	MutableCaptureConfiguration().script_path = Helper::String::NormalizeUtf8(absl::string_view(script_path.data(), script_path.size()));
	MutableCaptureConfiguration().dump_dir = Helper::String::DefaultString(absl::string_view(dump_dir.data(), dump_dir.size()), ".");
	if (!MutableCaptureConfiguration().script_path.empty()) {
		CrashHandler::SetScriptPath(MutableCaptureConfiguration().script_path);
	}
	CrashHandler::SetDumpDir(MutableCaptureConfiguration().dump_dir);
}

void BindIsolate(v8::Isolate* isolate) {
	CrashHandler::SetIsolate(isolate);
}

CaptureConfiguration CurrentCaptureConfiguration() {
	std::lock_guard<std::mutex> lock(CaptureMutex());
	return MutableCaptureConfiguration();
}

std::string LastCrashDumpPath() {
	return CrashHandler::LastDumpPath();
}

}  // namespace XER::ErrorHandler
