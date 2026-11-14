#pragma once

// xer.audio.synth.Filter — wraps XER::Audio::Synth::SynthFilterLadder.

#include <memory>

#include "audio/synth_engine/synth_filter_ladder.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class FilterWrap : public Nan::ObjectWrap {
public:
    explicit FilterWrap();
    ~FilterWrap() override;

    XER::Audio::Synth::SynthFilterLadder& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetCutoff(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetResonance(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthFilterLadder a_;
};

FilterWrap* UnwrapFilter(v8::Local<v8::Value> v);
void InitSynthFilter(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
