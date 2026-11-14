// xer.audio.plugin.Vst3External — Vst3ExternalHost binding.

#include "vst3_external.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::Vst3ExternalHost;

Vst3ExtWrap::Vst3ExtWrap() = default;
Vst3ExtWrap::~Vst3ExtWrap() = default;

Vst3ExtWrap* UnwrapVst3Ext(v8::Local<v8::Value> v) {
    return UnwrapT<Vst3ExtWrap>(v);
}

void Vst3ExtWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new Vst3ExtWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void Vst3ExtWrap::Load(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Load(
        StrArg(info, 0), NumArg(info, 1, 44100.0),
        static_cast<uint32_t>(IntArg(info, 2, 512)))));
}

void Vst3ExtWrap::Unload(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    if (!w) return;
    w->Get().Unload();
}

void Vst3ExtWrap::IsLoaded(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsLoaded()));
}

void Vst3ExtWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Vst3External.Process expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().Process(in.data(), out.data(),
                         static_cast<uint32_t>(in.size()))) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void Vst3ExtWrap::SetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void Vst3ExtWrap::GetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetParameter(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void Vst3ExtWrap::GetParameters(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().GetParameters()));
}

void Vst3ExtWrap::GetPluginName(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Ext(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetPluginName()).ToLocalChecked());
}


void InitPluginVst3External(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Vst3External", Vst3ExtWrap::New,
        {{"Load", Vst3ExtWrap::Load},
         {"Unload", Vst3ExtWrap::Unload},
         {"IsLoaded", Vst3ExtWrap::IsLoaded},
         {"Process", Vst3ExtWrap::Process},
         {"SetParameter", Vst3ExtWrap::SetParameter},
         {"GetParameter", Vst3ExtWrap::GetParameter},
         {"GetParameters", Vst3ExtWrap::GetParameters},
         {"GetPluginName", Vst3ExtWrap::GetPluginName}});
}

} // namespace xer::addon::audio::plugin
