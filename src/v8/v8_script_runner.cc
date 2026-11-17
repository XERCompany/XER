#include "v8/v8_script_runner.h"

#include <string>

#include "v8/v8_exception_handler.h"

namespace XER::V8Runtime {

bool CompileAndRun(v8::Isolate* isolate,
		   v8::Local<v8::Context> context,
		   std::string_view source_text,
		   std::string_view script_name,
		   v8::Local<v8::Value>* result_out,
		   std::string* error_out) {
	v8::TryCatch try_catch(isolate);
	v8::Local<v8::String> source_string = v8::String::NewFromUtf8(
		isolate,
		source_text.data(),
		v8::NewStringType::kNormal,
		static_cast<int>(source_text.size())).ToLocalChecked();
	v8::Local<v8::String> script_name_string = v8::String::NewFromUtf8(
		isolate,
		script_name.data(),
		v8::NewStringType::kNormal,
		static_cast<int>(script_name.size())).ToLocalChecked();
	v8::ScriptOrigin origin(isolate, script_name_string);
	v8::Local<v8::Script> script;
	if (!v8::Script::Compile(context, source_string, &origin).ToLocal(&script)) {
		if (error_out != nullptr) {
			*error_out = BuildExceptionReport(isolate, context, try_catch, "script compile", source_text);
		}
		return false;
	}
	if (result_out == nullptr) {
		v8::Local<v8::Value> ignored;
		result_out = &ignored;
	}
	if (!script->Run(context).ToLocal(result_out)) {
		if (error_out != nullptr) {
			*error_out = BuildExceptionReport(isolate, context, try_catch, "script runtime", source_text);
		}
		return false;
	}
	return true;
}

}  // namespace XER::V8Runtime
