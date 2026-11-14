#pragma once

// xer.audio.fx.ParametricEQ — wraps XER::Audio::FX::ParametricEQ.

#include <memory>

#include "audio/effects_rack/fx_eq_parametric.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class ParametricEQWrap : public Nan::ObjectWrap {
public:
    explicit ParametricEQWrap();
    ~ParametricEQWrap() override;

    XER::Audio::FX::ParametricEQ& Get() { return eq_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetBand(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBand(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ParametricEQ eq_;
};

ParametricEQWrap* UnwrapParametricEQ(v8::Local<v8::Value> v);
void InitFxParametricEQ(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
