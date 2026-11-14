#pragma once

// xer.audio.fx.ReverbAlgo — wraps XER::Audio::FX::ReverbAlgorithmic.

#include <memory>

#include "audio/effects_rack/fx_reverb_algorithmic.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class ReverbAlgoWrap : public Nan::ObjectWrap {
public:
    explicit ReverbAlgoWrap();
    ~ReverbAlgoWrap() override;

    XER::Audio::FX::ReverbAlgorithmic& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRoomSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDamping(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ReverbAlgorithmic fx_;
};

ReverbAlgoWrap* UnwrapReverbAlgo(v8::Local<v8::Value> v);
void InitFxReverbAlgo(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
