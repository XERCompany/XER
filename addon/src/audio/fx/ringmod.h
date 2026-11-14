#pragma once

// xer.audio.fx.RingMod — wraps XER::Audio::FX::ModRingMod.

#include <memory>

#include "audio/effects_rack/fx_mod_extra.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class RingModWrap : public Nan::ObjectWrap {
public:
    explicit RingModWrap();
    ~RingModWrap() override;

    XER::Audio::FX::ModRingMod& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetFrequencyHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ModRingMod fx_;
};

RingModWrap* UnwrapRingMod(v8::Local<v8::Value> v);
void InitFxRingMod(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
