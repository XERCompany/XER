#pragma once

// xer.audio.fx.GraphicEQ — wraps XER::Audio::FX::GraphicEQ.

#include <memory>

#include "audio/effects_rack/fx_eq_graphic.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::fx {

class GraphicEQWrap : public Nan::ObjectWrap {
public:
    explicit GraphicEQWrap();
    ~GraphicEQWrap() override;

    XER::Audio::FX::GraphicEQ& Get() { return eq_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetBandGainDb(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::FX::GraphicEQ eq_;
};

GraphicEQWrap* UnwrapGraphicEQ(v8::Local<v8::Value> v);
void InitFxGraphicEQ(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::fx
