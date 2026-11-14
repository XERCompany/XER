#pragma once

// xer.audio.fx.Bitcrush — wraps XER::Audio::FX::DistortionBitcrush.

#include <memory>

#include "audio/effects_rack/fx_distortion_bitcrush.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class BitcrushWrap : public Nan::ObjectWrap {
public:
    explicit BitcrushWrap();
    ~BitcrushWrap() override;

    XER::Audio::FX::DistortionBitcrush& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetBitDepth(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDownsampleFactor(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::DistortionBitcrush fx_;
};

BitcrushWrap* UnwrapBitcrush(v8::Local<v8::Value> v);
void InitFxBitcrush(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
