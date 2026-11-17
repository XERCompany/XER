#include "flow_script_event_bus.h"

#include "flow_script_console.h"

#include "javascript/common/compression_codec.h"

namespace XER {
namespace flow_script_detail::event_bus_detail {

JsEventBusState* GetEventBusState(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Local<v8::Value> data = args.Data();
    if (data.IsEmpty() || !data->IsExternal()) {
        return nullptr;
    }
    return static_cast<JsEventBusState*>(v8::External::Cast(*data)->Value());
}

void EventBusRegisterHandlerCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    JsEventBusState* state = GetEventBusState(args);
    if (state == nullptr) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "EventBus state is unavailable")));
        return;
    }

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "registerHandler(name, fn) expects (string, function)")));
        return;
    }

    const std::string name = Utf8(isolate, args[0]);
    const bool ok = state->bridge.Register(name, args[1].As<v8::Function>());
    args.GetReturnValue().Set(v8::Boolean::New(isolate, ok));
}

void EventBusSubscribeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    JsEventBusState* state = GetEventBusState(args);
    if (state == nullptr) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "EventBus state is unavailable")));
        return;
    }

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "subscribe(topic, handler) expects (string, string)")));
        return;
    }

    const std::string topic = Utf8(isolate, args[0]);
    const std::string handler_name = Utf8(isolate, args[1]);
    const std::uint64_t token = state->router.Subscribe(topic, handler_name);
    args.GetReturnValue().Set(v8::Number::New(isolate, static_cast<double>(token)));
}

void EventBusPublishCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    JsEventBusState* state = GetEventBusState(args);
    if (state == nullptr) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "EventBus state is unavailable")));
        return;
    }

    if (args.Length() < 2 || !args[0]->IsString() || !args[1]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "publish(topic, text) expects (string, string)")));
        return;
    }

    const std::string topic = Utf8(isolate, args[0]);
    const std::string text = Utf8(isolate, args[1]);
    const std::size_t delivered = state->pipeline.PublishRaw(topic, text);
    args.GetReturnValue().Set(v8::Integer::New(isolate, static_cast<int32_t>(delivered)));
}

void EventBusUnsubscribeCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    JsEventBusState* state = GetEventBusState(args);
    if (state == nullptr) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "EventBus state is unavailable")));
        return;
    }

    if (args.Length() < 1 || !args[0]->IsNumber()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "unsubscribe(token) expects number token")));
        return;
    }

    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const double raw = args[0]->NumberValue(context).FromMaybe(0.0);
    if (raw <= 0.0) {
        args.GetReturnValue().Set(v8::Boolean::New(isolate, false));
        return;
    }

    const bool removed = state->router.Unsubscribe(static_cast<std::uint64_t>(raw));
    args.GetReturnValue().Set(v8::Boolean::New(isolate, removed));
}

void EventBusCompressCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);

    if (args.Length() < 1 || !args[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "compress(text) expects string")));
        return;
    }

    const std::string text = Utf8(isolate, args[0]);
    auto compressed = engine::javascript::common::CompressionCodec::Compress(text);
    if (!compressed.has_value()) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Compression failed")));
        return;
    }

    args.GetReturnValue().Set(ByteVectorToUint8Array(isolate, *compressed));
}

void EventBusDecompressCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (args.Length() < 1) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "decompress(data) expects byte source")));
        return;
    }

    std::vector<std::uint8_t> bytes;
    if (!ValueToByteVector(context, args[0], &bytes)) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate,
            "decompress expects Uint8Array, ArrayBuffer or number[]")));
        return;
    }

    auto text = engine::javascript::common::CompressionCodec::DecompressToString(bytes);
    if (!text.has_value()) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "Decompression failed")));
        return;
    }

    args.GetReturnValue().Set(v8::String::NewFromUtf8(isolate, text->c_str()).ToLocalChecked());
}

void EventBusPublishCompressedCallback(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    JsEventBusState* state = GetEventBusState(args);
    if (state == nullptr) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(isolate, "EventBus state is unavailable")));
        return;
    }

    if (args.Length() < 2 || !args[0]->IsString()) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate,
            "publishCompressed(topic, data) expects (string, bytes)")));
        return;
    }

    std::vector<std::uint8_t> bytes;
    if (!ValueToByteVector(context, args[1], &bytes)) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
            isolate,
            "publishCompressed expects Uint8Array, ArrayBuffer or number[]")));
        return;
    }

    const std::string topic = Utf8(isolate, args[0]);
    const std::size_t delivered = state->pipeline.PublishCompressed(topic, bytes);
    args.GetReturnValue().Set(v8::Integer::New(isolate, static_cast<int32_t>(delivered)));
}

}  // namespace flow_script_detail::event_bus_detail

}  // namespace XER
