#pragma once

// xer.audio.fx.Limiter — wraps XER::Audio::FX::DynamicsLimiter.

#include <memory>

#include "audio/effects_rack/fx_dynamics_limiter.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class LimiterWrap : public Nan::ObjectWrap {
public:
    explicit LimiterWrap();
    ~LimiterWrap() override;

    XER::Audio::FX::DynamicsLimiter& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetCeilingDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRelease(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::DynamicsLimiter fx_;
};

LimiterWrap* UnwrapLimiter(v8::Local<v8::Value> v);
void InitFxLimiter(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
