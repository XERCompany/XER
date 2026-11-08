#pragma once

#include <string>
#include <string_view>

#include <v8.h>

namespace XER::V8Runtime {

bool CompileAndRun(v8::Isolate* isolate,
		   v8::Local<v8::Context> context,
		   std::string_view source_text,
		   std::string_view script_name,
		   v8::Local<v8::Value>* result_out,
		   std::string* error_out);

}  // namespace XER::V8Runtime
