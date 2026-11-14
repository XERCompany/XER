// xer.audio.midi.Mapping — MidiControllerMapping binding.

#include "mapping.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiControllerMapping;

MappingWrap::MappingWrap() = default;
MappingWrap::~MappingWrap() = default;

MappingWrap* UnwrapMapping(v8::Local<v8::Value> v) {
    return UnwrapT<MappingWrap>(v);
}

void MappingWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new MappingWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void MappingWrap::Bind(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMapping(info.This());
    if (!w) return;
    w->Get().Bind(
        static_cast<uint8_t>(IntArg(info, 0, 0)),
        static_cast<uint32_t>(IntArg(info, 1, 0)),
        static_cast<float>(NumArg(info, 2, 0.0)),
        static_cast<float>(NumArg(info, 3, 1.0)),
        BoolArg(info, 4, false),
        IntArg(info, 5, -1),
        StrArg(info, 6));
}

void MappingWrap::Resolve(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMapping(info.This());
    if (!w) return;
    uint32_t pid = 0;
    if (w->Get().Resolve(static_cast<uint8_t>(IntArg(info, 0, 0)), pid)) {
        info.GetReturnValue().Set(Nan::New(static_cast<double>(pid)));
    }
}

void MappingWrap::Clear(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMapping(info.This());
    if (!w) return;
    w->Get().Clear();
}


void InitMidiMapping(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Mapping", MappingWrap::New,
        {{"Bind", MappingWrap::Bind},
         {"Resolve", MappingWrap::Resolve},
         {"Clear", MappingWrap::Clear}});
}

} // namespace xer::addon::audio::midi
