#pragma once

// xer.audio.synth.OscAnalog — wraps XER::Audio::Synth::SynthOscAnalog.

#include <memory>

#include "audio/synth_engine/synth_osc_analog.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class OscAnalogWrap : public Nan::ObjectWrap {
public:
    explicit OscAnalogWrap();
    ~OscAnalogWrap() override;

    XER::Audio::Synth::SynthOscAnalog& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetFrequency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthOscAnalog a_;
};

OscAnalogWrap* UnwrapOscAnalog(v8::Local<v8::Value> v);
void InitSynthOscAnalog(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
