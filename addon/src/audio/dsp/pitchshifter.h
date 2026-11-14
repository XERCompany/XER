#pragma once

// xer.audio.dsp.PitchShifter — wraps XER::Audio::DSP::GranularPitchShifter.

#include <memory>

#include "audio/dsp_algorithms/dsp_pitch_shifter_granular.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class PitchShifterWrap : public Nan::ObjectWrap {
public:
    explicit PitchShifterWrap();
    ~PitchShifterWrap() override;

    XER::Audio::DSP::GranularPitchShifter& Get() { return shifter_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetPitchRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPitchRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetPitchSemitones(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessBlock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::GranularPitchShifter shifter_;
};

PitchShifterWrap* UnwrapPitchShifter(v8::Local<v8::Value> v);
void InitDspPitchShifter(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
