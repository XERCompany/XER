#pragma once

// xer.audio.synth.OscWavetable — wraps XER::Audio::Synth::SynthOscWavetable.

#include <memory>

#include "audio/synth_engine/synth_osc_wavetable.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class OscWavetableWrap : public Nan::ObjectWrap {
public:
    explicit OscWavetableWrap();
    ~OscWavetableWrap() override;

    XER::Audio::Synth::SynthOscWavetable& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetTable(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void BuildBandlimitedTable(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetFrequency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthOscWavetable a_;
};

OscWavetableWrap* UnwrapOscWavetable(v8::Local<v8::Value> v);
void InitSynthOscWavetable(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
