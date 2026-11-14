#pragma once

// xer.audio.core.EngineHost — wraps XER::Audio::Core::AudioEngineHost.

#include <memory>

#include "audio/audio_core/audio_engine_host.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class EngineHostWrap : public Nan::ObjectWrap {
public:
    explicit EngineHostWrap();
    ~EngineHostWrap() override;

    XER::Audio::Core::AudioEngineHost& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void CreateEngine(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void DestroyEngine(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void StartAllEngines(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void StopAllEngines(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetEngineList(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetGlobalSampleRate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetGlobalBufferSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetEngineCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetHostInfo(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioEngineHost a_;
};

EngineHostWrap* UnwrapEngineHost(v8::Local<v8::Value> v);
void InitCoreEngineHost(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
