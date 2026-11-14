#pragma once

// xer.audio.core.Engine — wraps XER::Audio::Core::AudioEngine.

#include <memory>

#include "audio/audio_core/audio_engine.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class EngineWrap : public Nan::ObjectWrap {
public:
    explicit EngineWrap();
    ~EngineWrap() override;

    XER::Audio::Core::AudioEngine& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Start(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Stop(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsRunning(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsInitialized(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetBufferSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetSampleRate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSampleRate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBufferSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetChannelCount(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioEngine a_;
};

EngineWrap* UnwrapEngine(v8::Local<v8::Value> v);
void InitCoreEngine(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
