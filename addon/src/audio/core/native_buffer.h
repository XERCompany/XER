#pragma once

// xer.audio.core.NativeBuffer — wraps XER::Audio::Core::AudioBuffer.

#include <memory>

#include "audio/audio_core/audio_buffer_manager.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class NativeBufferWrap : public Nan::ObjectWrap {
public:
    explicit NativeBufferWrap(int sample_rate, int channels,
                           int bit_depth, int layout);
    ~NativeBufferWrap() override;

    XER::Audio::Core::AudioBuffer& Get() { return *a_; }
    const std::shared_ptr<XER::Audio::Core::AudioBuffer>& Shared() const {
        return a_;
    }

    // Fresh JS NativeBuffer wrapping an externally-owned AudioBuffer
    // (e.g. acquired from BufferManager). Returns empty Local on failure.
    static v8::Local<v8::Object> Adopt(
        std::shared_ptr<XER::Audio::Core::AudioBuffer> buf);

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Resize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFrameCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSampleCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetByteSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Fill(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFloatData(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    std::shared_ptr<XER::Audio::Core::AudioBuffer> a_;
    XER::Audio::Core::AudioBuffer& Buf() { return *a_; }};

NativeBufferWrap* UnwrapNativeBuffer(v8::Local<v8::Value> v);
void InitCoreBufferManager(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
