#pragma once

// xer.audio.core.AsyncManager — wraps XER::Audio::Core::AudioAsyncManager
// (singleton; synchronous control surface — async futures are not bridged).

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class AsyncManagerWrap : public Nan::ObjectWrap {
public:
    explicit AsyncManagerWrap();
    ~AsyncManagerWrap() override;

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Start(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Stop(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsRunning(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPendingOperationCount(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
};

void InitCoreAsyncManager(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
