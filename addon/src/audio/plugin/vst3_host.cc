// xer.audio.plugin.Vst3Host — Vst3HostInterface binding.

#include "vst3_host.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::Vst3HostInterface;

Vst3HostWrap::Vst3HostWrap() = default;
Vst3HostWrap::~Vst3HostWrap() = default;

Vst3HostWrap* UnwrapVst3Host(v8::Local<v8::Value> v) {
    return UnwrapT<Vst3HostWrap>(v);
}

void Vst3HostWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new Vst3HostWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void Vst3HostWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        NumArg(info, 0, 44100.0),
        static_cast<uint32_t>(IntArg(info, 1, 512)))));
}

void Vst3HostWrap::LoadPlugin(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().LoadPlugin(
        StrArg(info, 0))));
}

void Vst3HostWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Vst3Host.Process expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().Process(in.data(), out.data(),
                         static_cast<uint32_t>(in.size()))) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void Vst3HostWrap::SetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void Vst3HostWrap::GetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetParameter(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void Vst3HostWrap::GetParameters(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().GetParameters()));
}

void Vst3HostWrap::GetBackendMode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVst3Host(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetBackendMode()).ToLocalChecked());
}


void InitPluginVst3Host(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Vst3Host", Vst3HostWrap::New,
        {{"Initialize", Vst3HostWrap::Initialize},
         {"LoadPlugin", Vst3HostWrap::LoadPlugin},
         {"Process", Vst3HostWrap::Process},
         {"SetParameter", Vst3HostWrap::SetParameter},
         {"GetParameter", Vst3HostWrap::GetParameter},
         {"GetParameters", Vst3HostWrap::GetParameters},
         {"GetBackendMode", Vst3HostWrap::GetBackendMode}});
}

} // namespace xer::addon::audio::plugin
