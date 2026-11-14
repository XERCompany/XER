#pragma once

// xer.audio.core.Fifo — wraps XER::Audio::Core::AudioFIFOQueue.

#include <memory>

#include "audio/audio_core/audio_fifo_queue.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class FifoWrap : public Nan::ObjectWrap {
public:
    explicit FifoWrap(size_t capacity);
    ~FifoWrap() override;

    XER::Audio::Core::AudioFIFOQueue& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Write(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Read(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAvailableFrames(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCapacity(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsEmpty(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsFull(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetTotalWritten(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetTotalRead(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioFIFOQueue a_;
};

FifoWrap* UnwrapFifo(v8::Local<v8::Value> v);
void InitCoreFifo(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
