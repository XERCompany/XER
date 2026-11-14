#pragma once

// xer.audio.fx.Tube — wraps XER::Audio::FX::DistortionTube.

#include <memory>

#include "audio/effects_rack/fx_distortion_tube.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class TubeWrap : public Nan::ObjectWrap {
public:
    explicit TubeWrap();
    ~TubeWrap() override;

    XER::Audio::FX::DistortionTube& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDrive(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetOutputGain(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::DistortionTube fx_;
};

TubeWrap* UnwrapTube(v8::Local<v8::Value> v);
void InitFxTube(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
