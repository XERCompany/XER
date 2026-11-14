// xer.audio.plugin.BuiltinHost — BuiltinPluginHost binding.

#include "builtin_host.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::BuiltinPluginHost;

BuiltinHostWrap::BuiltinHostWrap() = default;
BuiltinHostWrap::~BuiltinHostWrap() = default;

BuiltinHostWrap* UnwrapBuiltinHost(v8::Local<v8::Value> v) {
    return UnwrapT<BuiltinHostWrap>(v);
}

void BuiltinHostWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new BuiltinHostWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void BuiltinHostWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuiltinHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        NumArg(info, 0, 44100.0),
        static_cast<uint32_t>(IntArg(info, 1, 512)))));
}

void BuiltinHostWrap::Load(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuiltinHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Load(
        StrArg(info, 0))));
}

void BuiltinHostWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuiltinHost(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("BuiltinHost.Process expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().Process(in.data(), out.data(),
                         static_cast<uint32_t>(in.size()))) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void BuiltinHostWrap::SetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuiltinHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void BuiltinHostWrap::GetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuiltinHost(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetParameter(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void BuiltinHostWrap::GetParameters(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBuiltinHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().GetParameters()));
}


void InitPluginBuiltinHost(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "BuiltinHost", BuiltinHostWrap::New,
        {{"Initialize", BuiltinHostWrap::Initialize},
         {"Load", BuiltinHostWrap::Load},
         {"Process", BuiltinHostWrap::Process},
         {"SetParameter", BuiltinHostWrap::SetParameter},
         {"GetParameter", BuiltinHostWrap::GetParameter},
         {"GetParameters", BuiltinHostWrap::GetParameters},
         });
}

} // namespace xer::addon::audio::plugin
