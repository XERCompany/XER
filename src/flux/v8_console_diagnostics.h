#pragma once

#include <string>

#include <v8.h>

namespace XER::Flux::console {

void StartTimer(v8::Isolate* isolate, const std::string& label);

std::string EndTimer(v8::Isolate* isolate,
			     const std::string& label,
			     bool* found = nullptr);

std::string BuildTrace(v8::Isolate* isolate, const std::string& message = std::string());

}  // namespace XER::Flux::console