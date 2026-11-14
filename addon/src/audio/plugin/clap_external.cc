// xer.audio.plugin.ClapExternal — ClapExternalPluginHost binding.

#include "clap_external.h"

#include <vector>
#include <string>

namespace xer::addon::audio::plugin {

using XER::Audio::Plugin::ClapExternalPluginHost;

ClapExtWrap::ClapExtWrap() = default;
ClapExtWrap::~ClapExtWrap() = default;

ClapExtWrap* UnwrapClapExt(v8::Local<v8::Value> v) {
    return UnwrapT<ClapExtWrap>(v);
}

void ClapExtWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ClapExtWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ClapExtWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        NumArg(info, 0, 44100.0),
        static_cast<uint32_t>(IntArg(info, 1, 512)))));
}

void ClapExtWrap::Load(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Load(
        StrArg(info, 0))));
}

void ClapExtWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("ClapExternal.Process expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().Process(in.data(), out.data(),
                         static_cast<uint32_t>(in.size()))) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void ClapExtWrap::SetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetParameter(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)))));
}

void ClapExtWrap::GetParameter(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().GetParameter(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void ClapExtWrap::GetParameters(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    if (!w) return;
    info.GetReturnValue().Set(
        ParamInfosToJS(w->Get().GetParameters()));
}

void ClapExtWrap::GetLoadedIdentifier(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClapExt(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetLoadedIdentifier()).ToLocalChecked());
}


void InitPluginClapExternal(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ClapExternal", ClapExtWrap::New,
        {{"Initialize", ClapExtWrap::Initialize},
         {"Load", ClapExtWrap::Load},
         {"Process", ClapExtWrap::Process},
         {"SetParameter", ClapExtWrap::SetParameter},
         {"GetParameter", ClapExtWrap::GetParameter},
         {"GetParameters", ClapExtWrap::GetParameters},
         {"GetLoadedIdentifier", ClapExtWrap::GetLoadedIdentifier}});
}

} // namespace xer::addon::audio::plugin
