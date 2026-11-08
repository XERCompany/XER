#pragma once

#include <string>
#include <string_view>

#include <v8.h>

namespace XER::V8Runtime {

std::string BuildExceptionReport(v8::Isolate* isolate,
				 v8::Local<v8::Context> context,
				 const v8::TryCatch& try_catch,
				 std::string_view phase,
				 std::string_view source_text);

}  // namespace XER::V8Runtime
