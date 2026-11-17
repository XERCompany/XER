#pragma once

#include <v8.h>

#include <cstdint>
#include <string>
#include <vector>


namespace XER {
namespace flow_script_detail {

bool BindConsoleGlobals(v8::Isolate* isolate, v8::Local<v8::Context> context);

std::string Utf8(v8::Isolate* isolate, v8::Local<v8::Value> value);

std::string JoinArguments(v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& args);

void ConsoleLogCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleInfoCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleWarnCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleErrorCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleDirCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleAssertCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleTableCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleTimeCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleTimeEndCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleTraceCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleSnapshotCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void ConsoleClearSnapshotCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

bool ValueToByteVector(
    v8::Local<v8::Context> context,
    v8::Local<v8::Value> value,
    std::vector<std::uint8_t>* out);

v8::Local<v8::Uint8Array> ByteVectorToUint8Array(
    v8::Isolate* isolate,
    const std::vector<std::uint8_t>& bytes);

}  // namespace flow_script_detail
}  // namespace XER
