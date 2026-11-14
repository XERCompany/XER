#pragma once

// xer.audio.core.Resample — wraps XER::Audio::Core::AudioSampleRateConverter.

#include <memory>

#include "audio/audio_core/audio_sample_rate_converter.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class ResampleWrap : public Nan::ObjectWrap {
public:
    explicit ResampleWrap(int quality);
    ~ResampleWrap() override;

    XER::Audio::Core::AudioSampleRateConverter& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsInitialized(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Convert(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetInputRate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetOutputRate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetInfo(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioSampleRateConverter a_;
};

ResampleWrap* UnwrapResample(v8::Local<v8::Value> v);
void InitCoreSampleRate(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
