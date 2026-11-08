#include "v8/v8_exception_handler.h"

#include <string>

#include "helper/stack_error.h"

namespace XER::V8Runtime {

std::string BuildExceptionReport(v8::Isolate* isolate,
				 v8::Local<v8::Context> context,
				 const v8::TryCatch& try_catch,
				 std::string_view phase,
				 std::string_view source_text) {
	return StackError::BuildV8Report(isolate, context, try_catch, phase, source_text);
}

}  // namespace XER::V8Runtime
