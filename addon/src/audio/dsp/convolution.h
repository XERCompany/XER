#pragma once

// xer.audio.dsp.Convolution — wraps XER::Audio::DSP::ConvolutionEngine.

#include <memory>

#include "audio/dsp_algorithms/dsp_convolution_engine.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class ConvolutionWrap : public Nan::ObjectWrap {
public:
    explicit ConvolutionWrap();
    ~ConvolutionWrap() override;

    XER::Audio::DSP::ConvolutionEngine& Get() { return eng_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetImpulseResponse(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetImpulseSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::ConvolutionEngine eng_;
};

ConvolutionWrap* UnwrapConvolution(v8::Local<v8::Value> v);
void InitDspConvolution(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
