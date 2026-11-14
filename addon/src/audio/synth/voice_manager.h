#pragma once

// xer.audio.synth.VoiceManager — wraps XER::Audio::Synth::SynthVoiceManager.

#include <memory>

#include "audio/synth_engine/synth_voice_manager.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class VoiceManagerWrap : public Nan::ObjectWrap {
public:
    explicit VoiceManagerWrap();
    ~VoiceManagerWrap() override;

    XER::Audio::Synth::SynthVoiceManager& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetMaxVoices(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void NoteOn(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void NoteOff(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void RenderSample(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetActiveVoiceCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetResidentVoiceCount(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthVoiceManager a_;
};

VoiceManagerWrap* UnwrapVoiceManager(v8::Local<v8::Value> v);
void InitSynthVoiceManager(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
