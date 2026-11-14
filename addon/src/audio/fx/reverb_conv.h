#pragma once

// xer.audio.fx.ReverbConv — wraps XER::Audio::FX::ReverbConvolution.

#include <memory>

#include "audio/effects_rack/fx_reverb_convolution.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class ReverbConvWrap : public Nan::ObjectWrap {
public:
    explicit ReverbConvWrap();
    ~ReverbConvWrap() override;

    XER::Audio::FX::ReverbConvolution& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void LoadImpulseResponse(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ReverbConvolution fx_;
};

ReverbConvWrap* UnwrapReverbConv(v8::Local<v8::Value> v);
void InitFxReverbConv(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
