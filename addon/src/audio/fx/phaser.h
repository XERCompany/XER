#pragma once

// xer.audio.fx.Phaser — wraps XER::Audio::FX::ModPhaser.

#include <memory>

#include "audio/effects_rack/fx_mod_phaser.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class PhaserWrap : public Nan::ObjectWrap {
public:
    explicit PhaserWrap();
    ~PhaserWrap() override;

    XER::Audio::FX::ModPhaser& Get() { return fx_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRateHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDepth(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetFeedback(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::ModPhaser fx_;
};

PhaserWrap* UnwrapPhaser(v8::Local<v8::Value> v);
void InitFxPhaser(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
