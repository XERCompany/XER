#pragma once

#include <v8.h>

#include <string>
#include <string_view>
#include <vector>

namespace XER::Flux::console {

enum class Stream {
	kStdout,
	kStderr,
};

struct FormatOptions {
	int max_depth = 4;
	std::size_t max_collection_entries = 64;
	bool top_level_plain_strings = true;
};

std::string Utf8(v8::Isolate* isolate, v8::Local<v8::Value> value);

std::string JoinValues(v8::Isolate* isolate,
			       v8::Local<v8::Context> context,
			       const std::vector<v8::Local<v8::Value>>& values,
			       const FormatOptions& options = FormatOptions());

std::string JoinArguments(v8::Isolate* isolate,
			  const v8::FunctionCallbackInfo<v8::Value>& args,
			  const FormatOptions& options = FormatOptions());

void WriteLine(Stream stream, std::string_view text);

}  // namespace XER::Flux::console