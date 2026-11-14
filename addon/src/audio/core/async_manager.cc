// xer.audio.core.AsyncManager — AudioAsyncManager binding (singleton).

#include "async_manager.h"

#include "audio/audio_core/audio_async_manager.h"

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioAsyncManager;

AsyncManagerWrap::AsyncManagerWrap() = default;
AsyncManagerWrap::~AsyncManagerWrap() = default;

void AsyncManagerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AsyncManagerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void AsyncManagerWrap::Start(
    const Nan::FunctionCallbackInfo<v8::Value>&) {
    AudioAsyncManager::Instance().Start();
}

void AsyncManagerWrap::Stop(
    const Nan::FunctionCallbackInfo<v8::Value>&) {
    AudioAsyncManager::Instance().Stop();
}

void AsyncManagerWrap::IsRunning(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(
        Nan::New(AudioAsyncManager::Instance().IsRunning()));
}

void AsyncManagerWrap::GetPendingOperationCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    info.GetReturnValue().Set(Nan::New(static_cast<double>(
        AudioAsyncManager::Instance().GetPendingOperationCount())));
}


void InitCoreAsyncManager(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "AsyncManager", AsyncManagerWrap::New,
        {{"Start", AsyncManagerWrap::Start},
         {"Stop", AsyncManagerWrap::Stop},
         {"IsRunning", AsyncManagerWrap::IsRunning},
         {"GetPendingOperationCount",
          AsyncManagerWrap::GetPendingOperationCount}});
}

} // namespace xer::addon::audio::core
