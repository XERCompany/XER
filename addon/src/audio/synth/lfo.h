#pragma once

// xer.audio.synth.Lfo — wraps XER::Audio::Synth::SynthLfoStandard.

#include <memory>

#include "audio/synth_engine/synth_lfo_standard.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class LfoWrap : public Nan::ObjectWrap {
public:
    explicit LfoWrap();
    ~LfoWrap() override;

    XER::Audio::Synth::SynthLfoStandard& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetRateHz(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthLfoStandard a_;
};

LfoWrap* UnwrapLfo(v8::Local<v8::Value> v);
void InitSynthLfo(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
