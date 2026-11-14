#pragma once

// xer.audio.core.BufferManager — wraps XER::Audio::Core::AudioBufferManager
// (pooled AudioBuffer allocator). AcquireBuffer returns NativeBuffer.

#include <memory>

#include "audio/audio_core/audio_buffer_manager.h"

#include "audio/audio_util.h"
#include "native_buffer.h"

namespace xer::addon::audio::core {

class BufferManagerWrap : public Nan::ObjectWrap {
public:
    explicit BufferManagerWrap(size_t pool_size);
    ~BufferManagerWrap() override;

    XER::Audio::Core::AudioBufferManager& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    // AcquireBuffer({sampleRate, channelCount, bitDepth, layout}, frames)
    //   -> NativeBuffer
    static void AcquireBuffer(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    // ReleaseBuffer(nativeBuffer)
    static void ReleaseBuffer(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Resize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAvailableBuffers(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetTotalBuffers(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPoolStatus(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioBufferManager a_;
};

BufferManagerWrap* UnwrapBufferManager(v8::Local<v8::Value> v);
void InitCoreBufferPool(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
