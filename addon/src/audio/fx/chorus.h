#pragma once

// xer.audio.fx.Chorus — wraps XER::Audio::FX::ModChorus.

#include <memory>

#include "audio/effects_rack/fx_mod_chorus.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class ChorusWrap : public Nan::ObjectWrap {
public:
    explicit ChorusWrap();
    ~ChorusWrap() override;

    XER::Audio::FX::ModChorus& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRateHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDepthSamples(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ModChorus fx_;
};

ChorusWrap* UnwrapChorus(v8::Local<v8::Value> v);
void InitFxChorus(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
