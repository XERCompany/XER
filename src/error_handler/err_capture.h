#pragma once

#include <string>
#include <string_view>

namespace v8 {
class Isolate;
}

namespace XER::ErrorHandler {

struct CaptureConfiguration {
	std::string script_path;
	std::string dump_dir = ".";
};

void InitializeCrashCapture(const CaptureConfiguration& config);
void UpdateCrashContext(std::string_view script_path, std::string_view dump_dir);
void BindIsolate(v8::Isolate* isolate);
CaptureConfiguration CurrentCaptureConfiguration();
std::string LastCrashDumpPath();

}  // namespace XER::ErrorHandler
