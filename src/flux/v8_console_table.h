#pragma once

#include <string>

#include <v8.h>

namespace XER::Flux::console {

std::string RenderTable(v8::Isolate* isolate,
			v8::Local<v8::Context> context,
			v8::Local<v8::Value> input,
			v8::Local<v8::Value> columns_filter);

}  // namespace XER::Flux::console