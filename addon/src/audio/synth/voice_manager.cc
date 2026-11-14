// xer.audio.synth.VoiceManager — SynthVoiceManager binding.

#include "voice_manager.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthVoiceManager;

VoiceManagerWrap::VoiceManagerWrap() = default;
VoiceManagerWrap::~VoiceManagerWrap() = default;

VoiceManagerWrap* UnwrapVoiceManager(v8::Local<v8::Value> v) {
    return UnwrapT<VoiceManagerWrap>(v);
}

void VoiceManagerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new VoiceManagerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void VoiceManagerWrap::SetMaxVoices(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVoiceManager(info.This());
    if (!w) return;
    w->Get().SetMaxVoices(uint32_t(IntArg(info, 0, 16)));
}

void VoiceManagerWrap::NoteOn(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVoiceManager(info.This());
    if (!w) return;
    w->Get().NoteOn(static_cast<uint8_t>(IntArg(info, 0, 60)),
                    static_cast<float>(NumArg(info, 1, 440.0)));
}

void VoiceManagerWrap::NoteOff(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVoiceManager(info.This());
    if (!w) return;
    w->Get().NoteOff(static_cast<uint8_t>(IntArg(info, 0, 60)));
}

void VoiceManagerWrap::RenderSample(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVoiceManager(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().RenderSample(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}

void VoiceManagerWrap::GetActiveVoiceCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVoiceManager(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetActiveVoiceCount())));
}

void VoiceManagerWrap::GetResidentVoiceCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapVoiceManager(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetResidentVoiceCount())));
}


void InitSynthVoiceManager(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "VoiceManager", VoiceManagerWrap::New,
        {{"SetMaxVoices", VoiceManagerWrap::SetMaxVoices},
         {"NoteOn", VoiceManagerWrap::NoteOn},
         {"NoteOff", VoiceManagerWrap::NoteOff},
         {"RenderSample", VoiceManagerWrap::RenderSample},
         {"GetActiveVoiceCount", VoiceManagerWrap::GetActiveVoiceCount},
         {"GetResidentVoiceCount", VoiceManagerWrap::GetResidentVoiceCount}});
}

} // namespace xer::addon::audio::synth
