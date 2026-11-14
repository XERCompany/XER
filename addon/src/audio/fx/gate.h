#pragma once

// xer.audio.fx.Gate — wraps XER::Audio::FX::DynamicsGate.

#include <memory>

#include "audio/effects_rack/fx_dynamics_gate.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class GateWrap : public Nan::ObjectWrap {
public:
    explicit GateWrap();
    ~GateWrap() override;

    XER::Audio::FX::DynamicsGate& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetThresholdDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetHoldSamples(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::DynamicsGate fx_;
};

GateWrap* UnwrapGate(v8::Local<v8::Value> v);
void InitFxGate(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
