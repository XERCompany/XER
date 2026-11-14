// xer.audio.midi.Sequencer — MidiPatternSequencer binding.

#include "sequencer.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiPatternSequencer;

SequencerWrap::SequencerWrap() = default;
SequencerWrap::~SequencerWrap() = default;

SequencerWrap* UnwrapSequencer(v8::Local<v8::Value> v) {
    return UnwrapT<SequencerWrap>(v);
}

void SequencerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new SequencerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void SequencerWrap::SetPattern(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSequencer(info.This());
    if (!w) return;
    if (info.Length() < 1) return;
    w->pattern_copy_ = MidiEventsFromJS(info[0]);
    w->Get().SetPattern(w->pattern_copy_);
}

void SequencerWrap::GetPattern(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSequencer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(MidiEventsToJS(w->pattern_copy_));
}

void SequencerWrap::SetLoopLength(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSequencer(info.This());
    if (!w) return;
    w->Get().SetLoopLength(uint32_t(IntArg(info, 0, 0)));
}

void SequencerWrap::CollectRange(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSequencer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(MidiEventsToJS(w->Get().CollectRange(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<uint32_t>(IntArg(info, 1, 0)))));
}


void InitMidiSequencer(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Sequencer", SequencerWrap::New,
        {{"SetPattern", SequencerWrap::SetPattern},
         {"GetPattern", SequencerWrap::GetPattern},
         {"SetLoopLength", SequencerWrap::SetLoopLength},
         {"CollectRange", SequencerWrap::CollectRange}});
}

} // namespace xer::addon::audio::midi
