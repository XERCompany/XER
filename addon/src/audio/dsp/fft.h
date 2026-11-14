#pragma once

// xer.audio.dsp.FFT — wraps XER::Audio::DSP::FFTEngine.

#include <memory>

#include "audio/dsp_algorithms/dsp_fft_engine.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class FFTWrap : public Nan::ObjectWrap {
public:
    explicit FFTWrap(size_t fft_size);
    ~FFTWrap() override;

    XER::Audio::DSP::FFTEngine& Get() { return eng_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Forward(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Inverse(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMagnitude(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFftSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::FFTEngine eng_;
};

FFTWrap* UnwrapFFT(v8::Local<v8::Value> v);
void InitDspFFT(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
