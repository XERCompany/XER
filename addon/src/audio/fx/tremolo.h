#pragma once

// xer.audio.fx.Tremolo — wraps XER::Audio::FX::ModTremolo.

#include <memory>

#include "audio/effects_rack/fx_mod_extra.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class TremoloWrap : public Nan::ObjectWrap {
public:
    explicit TremoloWrap();
    ~TremoloWrap() override;

    XER::Audio::FX::ModTremolo& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRateHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDepth(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ModTremolo fx_;
};

TremoloWrap* UnwrapTremolo(v8::Local<v8::Value> v);
void InitFxTremolo(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
