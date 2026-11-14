// xer.audio.plugin.ClapHost — ClapHostInterface binding.

#include "clap_host.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::ClapHostInterface;

ClapHostWrap::ClapHostWrap() = default;
ClapHostWrap::~ClapHostWrap() = default;

ClapHostWrap* UnwrapClapHost(v8::Local<v8::Value> v) {
    return UnwrapT<ClapHostWrap>(v);
}

void ClapHostWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ClapHostWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ClapHostWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        NumArg(info, 0, 44100.0),
        static_cast<uint32_t>(IntArg(info, 1, 512)))));
}

void ClapHostWrap::LoadPlugin(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().LoadPlugin(
        StrArg(info, 0))));
}

void ClapHostWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapHost(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("ClapHost.Process expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().Process(in.data(), out.data(),
                         static_cast<uint32_t>(in.size()))) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void ClapHostWrap::SetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void ClapHostWrap::GetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapHost(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetParameter(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void ClapHostWrap::GetParameters(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().GetParameters()));
}


void InitPluginClapHost(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ClapHost", ClapHostWrap::New,
        {{"Initialize", ClapHostWrap::Initialize},
         {"LoadPlugin", ClapHostWrap::LoadPlugin},
         {"Process", ClapHostWrap::Process},
         {"SetParameter", ClapHostWrap::SetParameter},
         {"GetParameter", ClapHostWrap::GetParameter},
         {"GetParameters", ClapHostWrap::GetParameters},
         });
}

} // namespace xer::addon::audio::plugin
