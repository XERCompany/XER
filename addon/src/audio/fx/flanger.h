#pragma once

// xer.audio.fx.Flanger — wraps XER::Audio::FX::ModFlanger.

#include <memory>

#include "audio/effects_rack/fx_mod_flanger.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class FlangerWrap : public Nan::ObjectWrap {
public:
    explicit FlangerWrap();
    ~FlangerWrap() override;

    XER::Audio::FX::ModFlanger& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRateHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDepthSamples(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetFeedback(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ModFlanger fx_;
};

FlangerWrap* UnwrapFlanger(v8::Local<v8::Value> v);
void InitFxFlanger(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
