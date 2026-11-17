#include "flow_script_event_bus.h"

namespace XER {
namespace flow_script_detail::event_bus_detail {

void EventBusRegisterHandlerCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void EventBusSubscribeCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void EventBusPublishCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void EventBusUnsubscribeCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void EventBusCompressCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void EventBusDecompressCallback(const v8::FunctionCallbackInfo<v8::Value>& args);
void EventBusPublishCompressedCallback(const v8::FunctionCallbackInfo<v8::Value>& args);

}  // namespace flow_script_detail::event_bus_detail

namespace flow_script_detail {

bool BindEventBus(v8::Isolate* isolate, v8::Local<v8::Context> context, JsEventBusState* state) {
    v8::Local<v8::Object> event_bus = v8::Object::New(isolate);
    v8::Local<v8::External> data = v8::External::New(isolate, state);

    bool bound = event_bus
                     ->Set(
                         context,
                         v8::String::NewFromUtf8Literal(isolate, "registerHandler"),
                         v8::Function::New(context, event_bus_detail::EventBusRegisterHandlerCallback, data)
                             .ToLocalChecked())
                     .FromMaybe(false);
    bound = bound
            && event_bus
                   ->Set(
                       context,
                       v8::String::NewFromUtf8Literal(isolate, "subscribe"),
                       v8::Function::New(context, event_bus_detail::EventBusSubscribeCallback, data).ToLocalChecked())
                   .FromMaybe(false);
    bound = bound
            && event_bus
                   ->Set(
                       context,
                       v8::String::NewFromUtf8Literal(isolate, "publish"),
                       v8::Function::New(context, event_bus_detail::EventBusPublishCallback, data).ToLocalChecked())
                   .FromMaybe(false);
    bound = bound
            && event_bus
                   ->Set(
                       context,
                       v8::String::NewFromUtf8Literal(isolate, "unsubscribe"),
                       v8::Function::New(context, event_bus_detail::EventBusUnsubscribeCallback, data).ToLocalChecked())
                   .FromMaybe(false);
    bound = bound
            && event_bus
                   ->Set(
                       context,
                       v8::String::NewFromUtf8Literal(isolate, "compress"),
                       v8::Function::New(context, event_bus_detail::EventBusCompressCallback).ToLocalChecked())
                   .FromMaybe(false);
    bound = bound
            && event_bus
                   ->Set(
                       context,
                       v8::String::NewFromUtf8Literal(isolate, "decompress"),
                       v8::Function::New(context, event_bus_detail::EventBusDecompressCallback).ToLocalChecked())
                   .FromMaybe(false);
    bound = bound
            && event_bus
                   ->Set(
                       context,
                       v8::String::NewFromUtf8Literal(isolate, "publishCompressed"),
                       v8::Function::New(context, event_bus_detail::EventBusPublishCompressedCallback, data)
                           .ToLocalChecked())
                   .FromMaybe(false);
    bound = bound
            && context->Global()
                   ->Set(context, v8::String::NewFromUtf8Literal(isolate, "EventBus"), event_bus)
                   .FromMaybe(false);

    return bound;
}

}  // namespace flow_script_detail
}  // namespace XER

