#include "flow_script_console.h"

#include <string>

#include "flow_script_event_bus_static.h"
#include "flux/v8_console_diagnostics.h"
#include "flux/v8_console_table.h"
#include "flux/v8_console_terminal.h"
#include "flux/v8_console_runtime.h"
#include "helper/class_builder.h"


namespace XER {
namespace flow_script_detail {
namespace {

std::string DefaultConsoleLabel(v8::Isolate* isolate,
					const v8::FunctionCallbackInfo<v8::Value>& args,
					int index) {
	if (index >= args.Length() || args[index]->IsUndefined()) {
		return "default";
	}
	return Utf8(isolate, args[index]);
}

void ConsoleConstructor(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	if (!args.IsConstructCall()) {
		v8::Local<v8::Value> console_ctor_value;
		if (!context->Global()->Get(context, Engine::Helper::ToV8Str(isolate, "Console")).ToLocal(&console_ctor_value)
			|| !console_ctor_value->IsFunction()) {
			Engine::Helper::ThrowError(isolate, "Console constructor is unavailable");
			return;
		}
		v8::Local<v8::Object> instance = console_ctor_value.As<v8::Function>()->NewInstance(context).ToLocalChecked();
		args.GetReturnValue().Set(instance);
		return;
	}
	args.GetReturnValue().Set(args.This());
}

std::string JoinArgumentsFromIndex(v8::Isolate* isolate,
				    const v8::FunctionCallbackInfo<v8::Value>& args,
				    int start_index) {
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	std::vector<v8::Local<v8::Value>> values;
	for (int index = start_index; index < args.Length(); ++index) {
		values.push_back(args[index]);
	}
	return flux::console::JoinValues(isolate, context, values);
}

v8::Local<v8::FunctionTemplate> MakeConsoleTemplate(v8::Isolate* isolate) {
	return Engine::Helper::MakeClass(
		isolate,
		"console",
		&ConsoleConstructor,
		{{"log", &ConsoleLogCallback},
		 {"info", &ConsoleInfoCallback},
		 {"warn", &ConsoleWarnCallback},
		 {"error", &ConsoleErrorCallback},
		 {"dir", &ConsoleDirCallback},
		 {"assert", &ConsoleAssertCallback},
		 {"table", &ConsoleTableCallback},
		 {"time", &ConsoleTimeCallback},
		 {"timeEnd", &ConsoleTimeEndCallback},
		 {"trace", &ConsoleTraceCallback},
		 {"snapshot", &ConsoleSnapshotCallback},
		 {"clearSnapshot", &ConsoleClearSnapshotCallback}},
		{},
		{},
		0);
}

}  // namespace

bool BindConsoleGlobals(v8::Isolate* isolate, v8::Local<v8::Context> context) {
	v8::Local<v8::FunctionTemplate> console_template = MakeConsoleTemplate(isolate);
	if (!Engine::Helper::ExportClass(isolate, context, "Console", console_template)) {
		return false;
	}
	v8::Local<v8::Value> console_ctor_value;
	if (!context->Global()->Get(context, Engine::Helper::ToV8Str(isolate, "Console")).ToLocal(&console_ctor_value)
		|| !console_ctor_value->IsFunction()) {
		return false;
	}
	v8::Local<v8::Object> console_instance = console_ctor_value.As<v8::Function>()->NewInstance(context).ToLocalChecked();
	return context->Global()
		->Set(context, Engine::Helper::ToV8Str(isolate, "console"), console_instance)
		.FromMaybe(false);
}

std::string Utf8(v8::Isolate* isolate, v8::Local<v8::Value> value) {
	return flux::console::Utf8(isolate, value);
}

std::string JoinArguments(v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& args) {
	return flux::console::JoinArguments(isolate, args);
}

std::string replaceBusEvent(std::string type, std::string msg) {
	if (!flow_script_detail::StaticEvent::state) {
		return msg;
	}
	v8::Isolate* isolate = flow_script_detail::StaticEvent::state->runtime.isolate();
	if (!isolate) {
		return msg;
	}
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	v8::Local<v8::Value> t_func;
	
	if (context->Global()->Get(context, Engine::Helper::ToV8Str(isolate, "t")).ToLocal(&t_func) 
		&& t_func->IsFunction()) {
		v8::Local<v8::Value> argv[] = {
			Engine::Helper::ToV8Str(isolate, type),
			Engine::Helper::ToV8Str(isolate, msg)
		};
		v8::Local<v8::Value> result;
		if (t_func.As<v8::Function>()->Call(context, context->Global(), 2, argv).ToLocal(&result) 
			&& result->IsString()) {
			msg = Utf8(isolate, result);
		}
	}
	
	context->Global()
		->Set(
			context,
			Engine::Helper::ToV8Str(isolate, "lastBusEventType"),
			Engine::Helper::ToV8Str(isolate, type))
		.FromMaybe(false);
	context->Global()
		->Set(
			context,
			Engine::Helper::ToV8Str(isolate, "lastBusEventMsg"),
			Engine::Helper::ToV8Str(isolate, msg))
		.FromMaybe(false);
	return msg;
}

void ConsoleLogCallback(const v8::FunctionCallbackInfo<v8::Value>& args) { 
	flux::console::WriteLine(flux::console::Stream::kStdout, replaceBusEvent("log", JoinArguments(args.GetIsolate(), args)));
}

void ConsoleInfoCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	flux::console::WriteLine(flux::console::Stream::kStdout, replaceBusEvent("info", JoinArguments(args.GetIsolate(), args)));
}

void ConsoleWarnCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	flux::console::WriteLine(flux::console::Stream::kStderr, replaceBusEvent("warn", JoinArguments(args.GetIsolate(), args)));
}

void ConsoleErrorCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	flux::console::WriteLine(flux::console::Stream::kStderr, replaceBusEvent("error", JoinArguments(args.GetIsolate(), args)));
}

void ConsoleDirCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	flux::console::WriteLine(flux::console::Stream::kStdout, JoinArguments(args.GetIsolate(), args));
}

void ConsoleAssertCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	if (args.Length() > 0 && args[0]->BooleanValue(isolate)) {
		return;
	}
	const std::string suffix = args.Length() > 1 ? JoinArgumentsFromIndex(isolate, args, 1) : std::string();
	const std::string message = suffix.empty() ? "Assertion failed" : "Assertion failed: " + suffix;
	flux::console::WriteLine(flux::console::Stream::kStderr, message);
}

void ConsoleTableCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	v8::Local<v8::Context> context = isolate->GetCurrentContext();
	if (args.Length() == 0) {
		return;
	}
	v8::Local<v8::Value> columns_filter = args.Length() > 1
		? v8::Local<v8::Value>(args[1])
		: v8::Local<v8::Value>(v8::Undefined(isolate));
	flux::console::WriteLine(
		flux::console::Stream::kStdout,
		flux::console::RenderTable(
			isolate,
			context,
			args[0],
			columns_filter));
}

void ConsoleTimeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	flux::console::StartTimer(isolate, DefaultConsoleLabel(isolate, args, 0));
}

void ConsoleTimeEndCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	const std::string label = DefaultConsoleLabel(isolate, args, 0);
	bool found = false;
	const std::string message = flux::console::EndTimer(isolate, label, &found);
	flux::console::WriteLine(
		found ? flux::console::Stream::kStdout : flux::console::Stream::kStderr,
		message);
}

void ConsoleTraceCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	flux::console::WriteLine(
		flux::console::Stream::kStderr,
		flux::console::BuildTrace(isolate, args.Length() > 0 ? JoinArguments(isolate, args) : std::string()));
}

void ConsoleSnapshotCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	v8::Isolate* isolate = args.GetIsolate();
	args.GetReturnValue().Set(
		Engine::Helper::ToV8Str(isolate, flux::console::SnapshotFromArgs(isolate, args)));
}

void ConsoleClearSnapshotCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
	flux::console::ClearSnapshotFromArgs(args.GetIsolate(), args);
}

}  // namespace flow_script_detail
}  // namespace XER

