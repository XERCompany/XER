#pragma once

// xer.audio.dsp.Biquad — wraps XER::Audio::DSP::BiquadProcessor.

#include <memory>

#include "audio/dsp_algorithms/dsp_biquad_processor.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class BiquadWrap : public Nan::ObjectWrap {
public:
    explicit BiquadWrap();
    ~BiquadWrap() override;

    XER::Audio::DSP::BiquadProcessor& Get() { return proc_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Configure(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessSample(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::BiquadProcessor proc_;
};

BiquadWrap* UnwrapBiquad(v8::Local<v8::Value> v);
void InitDspBiquad(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
