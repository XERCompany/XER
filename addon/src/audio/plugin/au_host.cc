// xer.audio.plugin.AuHost — AuHostInterface binding.

#include "au_host.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::AuHostInterface;

AuHostWrap::AuHostWrap() = default;
AuHostWrap::~AuHostWrap() = default;

AuHostWrap* UnwrapAuHost(v8::Local<v8::Value> v) {
    return UnwrapT<AuHostWrap>(v);
}

void AuHostWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AuHostWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void AuHostWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        NumArg(info, 0, 44100.0),
        static_cast<uint32_t>(IntArg(info, 1, 512)))));
}

void AuHostWrap::LoadComponent(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().LoadComponent(
        StrArg(info, 0))));
}

void AuHostWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("AuHost.Process expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().Process(in.data(), out.data(),
                         static_cast<uint32_t>(in.size()))) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void AuHostWrap::SetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void AuHostWrap::GetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetParameter(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void AuHostWrap::GetParameters(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().GetParameters()));
}

void AuHostWrap::GetBackendMode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAuHost(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetBackendMode()).ToLocalChecked());
}


void InitPluginAuHost(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "AuHost", AuHostWrap::New,
        {{"Initialize", AuHostWrap::Initialize},
         {"LoadComponent", AuHostWrap::LoadComponent},
         {"Process", AuHostWrap::Process},
         {"SetParameter", AuHostWrap::SetParameter},
         {"GetParameter", AuHostWrap::GetParameter},
         {"GetParameters", AuHostWrap::GetParameters},
         {"GetBackendMode", AuHostWrap::GetBackendMode}});
}

} // namespace xer::addon::audio::plugin
