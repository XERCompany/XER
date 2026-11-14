// xer.audio.core.EngineHost — AudioEngineHost binding.

#include "engine_host.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioEngineHost;

EngineHostWrap::EngineHostWrap() = default;
EngineHostWrap::~EngineHostWrap() = default;

EngineHostWrap* UnwrapEngineHost(v8::Local<v8::Value> v) {
    return UnwrapT<EngineHostWrap>(v);
}

void EngineHostWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new EngineHostWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void EngineHostWrap::CreateEngine(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().CreateEngine(
        StrArg(info, 0), IntArg(info, 1, 44100),
        IntArg(info, 2, 512), IntArg(info, 3, 2))));
}

void EngineHostWrap::DestroyEngine(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().DestroyEngine(
        StrArg(info, 0))));
}

void EngineHostWrap::StartAllEngines(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().StartAllEngines()));
}

void EngineHostWrap::StopAllEngines(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().StopAllEngines()));
}

void EngineHostWrap::GetEngineList(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(StrVecToJS(w->Get().GetEngineList()));
}

void EngineHostWrap::SetGlobalSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetGlobalSampleRate(
        IntArg(info, 0, 44100))));
}

void EngineHostWrap::SetGlobalBufferSize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetGlobalBufferSize(
        IntArg(info, 0, 512))));
}

void EngineHostWrap::GetEngineCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetEngineCount()));
}

void EngineHostWrap::GetHostInfo(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngineHost(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetHostInfo()).ToLocalChecked());
}


void InitCoreEngineHost(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "EngineHost", EngineHostWrap::New,
        {{"CreateEngine", EngineHostWrap::CreateEngine},
         {"DestroyEngine", EngineHostWrap::DestroyEngine},
         {"StartAllEngines", EngineHostWrap::StartAllEngines},
         {"StopAllEngines", EngineHostWrap::StopAllEngines},
         {"GetEngineList", EngineHostWrap::GetEngineList},
         {"SetGlobalSampleRate", EngineHostWrap::SetGlobalSampleRate},
         {"SetGlobalBufferSize", EngineHostWrap::SetGlobalBufferSize},
         {"GetEngineCount", EngineHostWrap::GetEngineCount},
         {"GetHostInfo", EngineHostWrap::GetHostInfo}});
}

} // namespace xer::addon::audio::core
