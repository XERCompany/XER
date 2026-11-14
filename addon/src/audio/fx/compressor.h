#pragma once

// xer.audio.fx.Compressor — wraps XER::Audio::FX::DynamicsCompressor.

#include <memory>

#include "audio/effects_rack/fx_dynamics_compressor.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class CompressorWrap : public Nan::ObjectWrap {
public:
    explicit CompressorWrap();
    ~CompressorWrap() override;

    XER::Audio::FX::DynamicsCompressor& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetSampleRate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetThresholdDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetAttackMs(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetReleaseMs(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::DynamicsCompressor fx_;
};

CompressorWrap* UnwrapCompressor(v8::Local<v8::Value> v);
void InitFxCompressor(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
