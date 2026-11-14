// xer.audio.midi.Clock — MidiClockGenerator binding.

#include "clock.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiClockGenerator;

ClockWrap::ClockWrap() = default;
ClockWrap::~ClockWrap() = default;

ClockWrap* UnwrapClock(v8::Local<v8::Value> v) {
    return UnwrapT<ClockWrap>(v);
}

void ClockWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ClockWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ClockWrap::SetTempoBpm(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClock(info.This());
    if (!w) return;
    w->Get().SetTempoBpm(NumArg(info, 0, 120.0));
}

void ClockWrap::AdvanceSamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClock(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        static_cast<double>(w->Get().AdvanceSamples(
            static_cast<uint32_t>(IntArg(info, 0, 0)),
            static_cast<uint32_t>(IntArg(info, 1, 44100))))));
}


void InitMidiClock(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Clock", ClockWrap::New,
        {{"SetTempoBpm", ClockWrap::SetTempoBpm},
         {"AdvanceSamples", ClockWrap::AdvanceSamples}});
}

} // namespace xer::addon::audio::midi
