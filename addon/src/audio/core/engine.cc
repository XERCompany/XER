// xer.audio.core.Engine — AudioEngine binding.

#include "engine.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioEngine;

EngineWrap::EngineWrap() = default;
EngineWrap::~EngineWrap() = default;

EngineWrap* UnwrapEngine(v8::Local<v8::Value> v) {
    return UnwrapT<EngineWrap>(v);
}

void EngineWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new EngineWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void EngineWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        IntArg(info, 0, 44100), IntArg(info, 1, 512),
        IntArg(info, 2, 2))));
}

void EngineWrap::Start(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Start()));
}

void EngineWrap::Stop(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Stop()));
}

void EngineWrap::IsRunning(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsRunning()));
}

void EngineWrap::IsInitialized(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsInitialized()));
}

void EngineWrap::SetBufferSize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetBufferSize(
        IntArg(info, 0, 512))));
}

void EngineWrap::SetSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SetSampleRate(
        IntArg(info, 0, 44100))));
}

void EngineWrap::GetSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetSampleRate()));
}

void EngineWrap::GetBufferSize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetBufferSize()));
}

void EngineWrap::GetChannelCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapEngine(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetChannelCount()));
}


void InitCoreEngine(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Engine", EngineWrap::New,
        {{"Initialize", EngineWrap::Initialize},
         {"Start", EngineWrap::Start},
         {"Stop", EngineWrap::Stop},
         {"IsRunning", EngineWrap::IsRunning},
         {"IsInitialized", EngineWrap::IsInitialized},
         {"SetBufferSize", EngineWrap::SetBufferSize},
         {"SetSampleRate", EngineWrap::SetSampleRate},
         {"GetSampleRate", EngineWrap::GetSampleRate},
         {"GetBufferSize", EngineWrap::GetBufferSize},
         {"GetChannelCount", EngineWrap::GetChannelCount}});
}

} // namespace xer::addon::audio::core
