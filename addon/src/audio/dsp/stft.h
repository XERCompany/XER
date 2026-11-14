#pragma once

// xer.audio.dsp.STFT — wraps XER::Audio::DSP::STFTProcessor.

#include <memory>

#include "audio/dsp_algorithms/dsp_stft_processor.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class STFTWrap : public Nan::ObjectWrap {
public:
    explicit STFTWrap(size_t fft_size, size_t hop_size,
                      XER::Audio::DSP::STFTWindowMode analysis_mode,
                      XER::Audio::DSP::STFTWindowMode synthesis_mode);
    ~STFTWrap() override;

    XER::Audio::DSP::STFTProcessor& Get() { return stft_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Analyze(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Synthesize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void RebuildHermitianSpectrum(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFftSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetHopSize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLatencySamples(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::STFTProcessor stft_;
};

STFTWrap* UnwrapSTFT(v8::Local<v8::Value> v);
void InitDspSTFT(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
