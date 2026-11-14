#pragma once

// xer.audio.fx.AutoTune — wraps XER::Audio::FX::AutoTuneCorrectPitch.

#include <memory>

#include "audio/effects_rack/fx_pitch_auto_tune_correct.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class AutoTuneWrap : public Nan::ObjectWrap {
public:
    explicit AutoTuneWrap();
    ~AutoTuneWrap() override;

    XER::Audio::FX::AutoTuneCorrectPitch& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetCorrectionAmount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetCorrectionSpeed(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRetuneSpeedMs(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetHumanize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetScaleRoot(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetScaleMode(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetPresenceDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetWetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::AutoTuneCorrectPitch fx_;
};

AutoTuneWrap* UnwrapAutoTune(v8::Local<v8::Value> v);
void InitFxAutoTune(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
