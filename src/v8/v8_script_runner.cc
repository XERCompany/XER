#include "v8/v8_script_runner.h"

#include <limits>
#include <string>

#include "v8/v8_exception_handler.h"

namespace Engine::V8Runtime {

bool CompileAndRun(v8::Isolate* isolate,
		   v8::Local<v8::Context> context,
		   std::string_view source_text,
		   std::string_view script_name,
		   v8::Local<v8::Value>* result_out,
		   std::string* error_out) {
	v8::TryCatch try_catch(isolate);
	const size_t max_v8_string_length = static_cast<size_t>(std::numeric_limits<int>::max());
	if (source_text.size() > max_v8_string_length || script_name.size() > max_v8_string_length) {
		if (error_out != nullptr) {
			*error_out = "Script source or name exceeds V8's maximum string length";
		}
		return false;
	}

	v8::Local<v8::String> source_string;
	if (!v8::String::NewFromUtf8(
		isolate,
		source_text.empty() ? "" : source_text.data(),
		v8::NewStringType::kNormal,
		static_cast<int>(source_text.size())).ToLocal(&source_string)) {
		if (error_out != nullptr) *error_out = "V8 failed to allocate script source";
		return false;
	}

	v8::Local<v8::String> script_name_string;
	if (!v8::String::NewFromUtf8(
		isolate,
		script_name.empty() ? "" : script_name.data(),
		v8::NewStringType::kNormal,
		static_cast<int>(script_name.size())).ToLocal(&script_name_string)) {
		if (error_out != nullptr) *error_out = "V8 failed to allocate script name";
		return false;
	}
	v8::ScriptOrigin origin(isolate, script_name_string);
	v8::Local<v8::Script> script;
	if (!v8::Script::Compile(context, source_string, &origin).ToLocal(&script)) {
		if (error_out != nullptr) {
			*error_out = BuildExceptionReport(isolate, context, try_catch, "script compile", source_text);
		}
		return false;
	}
	v8::Local<v8::Value> result;
	if (!script->Run(context).ToLocal(&result)) {
		if (error_out != nullptr) {
			*error_out = BuildExceptionReport(isolate, context, try_catch, "script runtime", source_text);
		}
		return false;
	}
	if (result_out != nullptr) *result_out = result;
	return true;
}

}  // namespace Engine::V8Runtime
