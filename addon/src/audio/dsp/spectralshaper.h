#pragma once

// xer.audio.dsp.SpectralShaper — wraps XER::Audio::DSP::SpectralShaper.

#include <memory>

#include "audio/dsp_algorithms/dsp_spectral_shaper.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class SpectralShaperWrap : public Nan::ObjectWrap {
public:
    explicit SpectralShaperWrap(size_t fft_size, size_t hop_size);
    ~SpectralShaperWrap() override;

    XER::Audio::DSP::SpectralShaper& Get() { return shaper_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetCurve(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCurve(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::SpectralShaper shaper_;
};

SpectralShaperWrap* UnwrapSpectralShaper(v8::Local<v8::Value> v);
void InitDspSpectralShaper(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
