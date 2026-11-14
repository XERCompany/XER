#pragma once

// xer.audio.fx.Vibrato — wraps XER::Audio::FX::ModVibrato.

#include <memory>

#include "audio/effects_rack/fx_mod_extra.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class VibratoWrap : public Nan::ObjectWrap {
public:
    explicit VibratoWrap();
    ~VibratoWrap() override;

    XER::Audio::FX::ModVibrato& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRateHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDepthMs(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ModVibrato fx_;
};

VibratoWrap* UnwrapVibrato(v8::Local<v8::Value> v);
void InitFxVibrato(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
