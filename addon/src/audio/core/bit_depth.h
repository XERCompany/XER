#pragma once

// xer.audio.core.BitDepth — wraps XER::Audio::Core::AudioBitDepthConverter.

#include <memory>

#include "audio/audio_core/audio_bit_depth_converter.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class BitDepthWrap : public Nan::ObjectWrap {
public:
    explicit BitDepthWrap();
    ~BitDepthWrap() override;

    XER::Audio::Core::AudioBitDepthConverter& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBytesPerSample(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsFloatingPoint(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsSupported(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void NormalizeSamples(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioBitDepthConverter a_;};

BitDepthWrap* UnwrapBitDepth(v8::Local<v8::Value> v);
void InitCoreBitDepth(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
