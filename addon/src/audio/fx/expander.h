#pragma once

// xer.audio.fx.Expander — wraps XER::Audio::FX::DynamicsExpander.

#include <memory>

#include "audio/effects_rack/fx_dynamics_expander.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class ExpanderWrap : public Nan::ObjectWrap {
public:
    explicit ExpanderWrap();
    ~ExpanderWrap() override;

    XER::Audio::FX::DynamicsExpander& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetThresholdDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRangeDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::DynamicsExpander fx_;
};

ExpanderWrap* UnwrapExpander(v8::Local<v8::Value> v);
void InitFxExpander(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
