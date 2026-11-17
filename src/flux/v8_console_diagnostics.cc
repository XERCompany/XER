#include "flux/v8_console_diagnostics.h"

#include <absl/strings/str_cat.h>
#include <absl/strings/str_format.h>

#include <chrono>
#include <string>
#include <unordered_map>

#include "flux/v8_console_runtime.h"

namespace flux::console {
namespace {

using ConsoleClock = std::chrono::steady_clock;
using TimerMap = std::unordered_map<std::string, ConsoleClock::time_point>;

std::unordered_map<v8::Isolate*, TimerMap>& ConsoleTimers() {
	static std::unordered_map<v8::Isolate*, TimerMap> timers;
	return timers;
}

std::string BuildStackTrace(v8::Isolate* isolate) {
	v8::Local<v8::StackTrace> stack = v8::StackTrace::CurrentStackTrace(isolate, 32, v8::StackTrace::kDetailed);
	if (stack.IsEmpty() || stack->GetFrameCount() == 0) {
		return std::string();
	}

	std::string out;
	for (int index = 0; index < stack->GetFrameCount(); ++index) {
		v8::Local<v8::StackFrame> frame = stack->GetFrame(isolate, index);
		if (frame.IsEmpty()) {
			continue;
		}
		v8::Local<v8::String> function_name_value = frame->GetFunctionName();
		v8::Local<v8::String> script_name_value = frame->GetScriptNameOrSourceURL();
		const std::string function_name = function_name_value.IsEmpty() ? std::string() : Utf8(isolate, function_name_value);
		std::string script_name = script_name_value.IsEmpty() ? std::string() : Utf8(isolate, script_name_value);
		if (script_name.empty()) {
			script_name = "<anonymous>";
		}
		if (!out.empty()) {
			absl::StrAppend(&out, "\n");
		}
		if (function_name.empty()) {
			absl::StrAppendFormat(&out, "    at %s:%d:%d", script_name, frame->GetLineNumber(), frame->GetColumn());
		} else {
			absl::StrAppendFormat(
				&out,
				"    at %s (%s:%d:%d)",
				function_name,
				script_name,
				frame->GetLineNumber(),
				frame->GetColumn());
		}
	}
	return out;
}

}  // namespace

void StartTimer(v8::Isolate* isolate, const std::string& label) {
	ConsoleTimers()[isolate][label] = ConsoleClock::now();
}

std::string EndTimer(v8::Isolate* isolate, const std::string& label, bool* found) {
	if (found != nullptr) {
		*found = false;
	}
	auto timers_it = ConsoleTimers().find(isolate);
	if (timers_it == ConsoleTimers().end()) {
		return absl::StrCat("Timer '", label, "' does not exist");
	}
	auto& timers = timers_it->second;
	const auto timer_it = timers.find(label);
	if (timer_it == timers.end()) {
		return absl::StrCat("Timer '", label, "' does not exist");
	}
	const double elapsed_ms = std::chrono::duration<double, std::milli>(ConsoleClock::now() - timer_it->second).count();
	timers.erase(timer_it);
	if (timers.empty()) {
		ConsoleTimers().erase(timers_it);
	}
	if (found != nullptr) {
		*found = true;
	}
	return absl::StrCat(label, ": ", absl::StrFormat("%.3fms", elapsed_ms));
}

std::string BuildTrace(v8::Isolate* isolate, const std::string& message) {
	const std::string prefix = message.empty() ? std::string("Trace") : absl::StrCat("Trace: ", message);
	const std::string stack = BuildStackTrace(isolate);
	return stack.empty() ? prefix : absl::StrCat(prefix, "\n", stack);
}

}  // namespace flux::console