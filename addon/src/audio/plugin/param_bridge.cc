// xer.audio.plugin.ParamBridge — PluginParameterBridge binding.

#include "param_bridge.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::PluginParameterBridge;

ParamBridgeWrap::ParamBridgeWrap() = default;
ParamBridgeWrap::~ParamBridgeWrap() = default;

ParamBridgeWrap* UnwrapParamBridge(v8::Local<v8::Value> v) {
    return UnwrapT<ParamBridgeWrap>(v);
}

void ParamBridgeWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ParamBridgeWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ParamBridgeWrap::Clear(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParamBridge(info.This());
    if (!w) return;
    w->Get().Clear();
}

void ParamBridgeWrap::RegisterParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParamBridge(info.This());
    if (!w) return;
    w->Get().RegisterParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        StrArg(info, 1),
        static_cast<float>(NumArg(info, 2, 0.0)),
        static_cast<float>(NumArg(info, 3, 0.0)),
        static_cast<float>(NumArg(info, 4, 1.0)));
}

void ParamBridgeWrap::SetValue(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParamBridge(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetValue(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void ParamBridgeWrap::SetNormalizedValue(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParamBridge(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetNormalizedValue(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void ParamBridgeWrap::GetValue(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParamBridge(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetValue(static_cast<uint32_t>(IntArg(info, 0, 0)), v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void ParamBridgeWrap::Snapshot(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParamBridge(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().Snapshot()));
}


void InitPluginParamBridge(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ParamBridge", ParamBridgeWrap::New,
        {{"Clear", ParamBridgeWrap::Clear},
         {"RegisterParameter", ParamBridgeWrap::RegisterParameter},
         {"SetValue", ParamBridgeWrap::SetValue},
         {"SetNormalizedValue", ParamBridgeWrap::SetNormalizedValue},
         {"GetValue", ParamBridgeWrap::GetValue},
         {"Snapshot", ParamBridgeWrap::Snapshot}});
}

} // namespace xer::addon::audio::plugin
