#include "flux/v8_console_terminal.h"

#include "flux/terminal/terminal_output_renderer.h"
#include "flux/v8_console_runtime.h"

namespace flux::console {
namespace {

v8::Local<v8::Value> ArgOrUndefined(v8::Isolate* isolate,
				    const v8::FunctionCallbackInfo<v8::Value>& args,
				    int index) {
	return index < args.Length()
		? v8::Local<v8::Value>(args[index])
		: v8::Local<v8::Value>(v8::Undefined(isolate));
}

flux::terminal::OutputStream ParseOutputStreamValue(v8::Isolate* isolate, v8::Local<v8::Value> value) {
	if (value.IsEmpty() || value->IsUndefined()) {
		return flux::terminal::OutputStream::kStdout;
	}
	return Utf8(isolate, value) == "stderr"
		? flux::terminal::OutputStream::kStderr
		: flux::terminal::OutputStream::kStdout;
}

}  // namespace

std::string SnapshotFromArgs(v8::Isolate* isolate,
			     const v8::FunctionCallbackInfo<v8::Value>& args,
			     int stream_arg_index) {
	return flux::terminal::Snapshot(ParseOutputStreamValue(isolate, ArgOrUndefined(isolate, args, stream_arg_index)));
}

void ClearSnapshotFromArgs(v8::Isolate* isolate,
			   const v8::FunctionCallbackInfo<v8::Value>& args,
			   int stream_arg_index) {
	v8::Local<v8::Value> value = ArgOrUndefined(isolate, args, stream_arg_index);
	if (value->IsUndefined()) {
		flux::terminal::ClearSnapshot();
		return;
	}
	flux::terminal::ClearSnapshot(ParseOutputStreamValue(isolate, value));
}

}  // namespace flux::console