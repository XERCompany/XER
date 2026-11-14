#pragma once

// xer.audio.dsp.DelayLine — wraps XER::Audio::DSP::CircularDelayLine.

#include <memory>

#include "audio/dsp_algorithms/dsp_delay_line_circular.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class DelayLineWrap : public Nan::ObjectWrap {
public:
    explicit DelayLineWrap();
    ~DelayLineWrap() override;

    XER::Audio::DSP::CircularDelayLine& Get() { return line_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDelaySamples(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetDelayMs(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessFractional(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMaxDelaySamples(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::CircularDelayLine line_;
};

DelayLineWrap* UnwrapDelayLine(v8::Local<v8::Value> v);
void InitDspDelayLine(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
