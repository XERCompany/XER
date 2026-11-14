#pragma once

// xer.audio.synth.OscFm — wraps XER::Audio::Synth::SynthOscFm.

#include <memory>

#include "audio/synth_engine/synth_osc_fm.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class OscFmWrap : public Nan::ObjectWrap {
public:
    explicit OscFmWrap();
    ~OscFmWrap() override;

    XER::Audio::Synth::SynthOscFm& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetCarrier(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetModulator(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetIndex(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Process(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthOscFm a_;
};

OscFmWrap* UnwrapOscFm(v8::Local<v8::Value> v);
void InitSynthOscFm(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
