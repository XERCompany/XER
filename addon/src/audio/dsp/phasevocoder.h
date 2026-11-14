#pragma once

// xer.audio.dsp.PhaseVocoder — wraps XER::Audio::DSP::PhaseVocoder.

#include <memory>

#include "audio/dsp_algorithms/dsp_phase_vocoder.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class PhaseVocoderWrap : public Nan::ObjectWrap {
public:
    explicit PhaseVocoderWrap(size_t fft_size, size_t hop_size);
    ~PhaseVocoderWrap() override;

    XER::Audio::DSP::PhaseVocoder& Get() { return pv_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetTimeStretchRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetTimeStretchRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::PhaseVocoder pv_;
};

PhaseVocoderWrap* UnwrapPhaseVocoder(v8::Local<v8::Value> v);
void InitDspPhaseVocoder(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
