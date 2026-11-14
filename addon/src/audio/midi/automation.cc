// xer.audio.midi.Automation — MidiAutomationCurve binding.

#include "automation.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiAutomationCurve;

AutomationWrap::AutomationWrap() = default;
AutomationWrap::~AutomationWrap() = default;

AutomationWrap* UnwrapAutomation(v8::Local<v8::Value> v) {
    return UnwrapT<AutomationWrap>(v);
}

void AutomationWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AutomationWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void AutomationWrap::AddPoint(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAutomation(info.This());
    if (!w) return;
    w->Get().AddPoint(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)));
}

void AutomationWrap::Evaluate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAutomation(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Evaluate(
        static_cast<uint32_t>(IntArg(info, 0, 0)))));
}


void InitMidiAutomation(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Automation", AutomationWrap::New,
        {{"AddPoint", AutomationWrap::AddPoint},
         {"Evaluate", AutomationWrap::Evaluate}});
}

} // namespace xer::addon::audio::midi
