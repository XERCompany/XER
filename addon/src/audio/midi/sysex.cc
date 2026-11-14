// xer.audio.midi.Sysex — MidiSysexHandler binding.

#include "sysex.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiSysexHandler;

SysexWrap::SysexWrap() = default;
SysexWrap::~SysexWrap() = default;

SysexWrap* UnwrapSysex(v8::Local<v8::Value> v) {
    return UnwrapT<SysexWrap>(v);
}

void SysexWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new SysexWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void SysexWrap::IsValidSysex(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSysex(info.This());
    if (!w) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) { info.GetReturnValue().Set(Nan::New(false)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().IsValidSysex(in)));
}

void SysexWrap::Parse(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSysex(info.This());
    if (!w) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Sysex.Parse expects Uint8Array");
        return;
    }
    XER::Audio::MIDI::MidiSysexMessage msg;
    if (!w->Get().Parse(in, &msg)) return;
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("manufacturerId").ToLocalChecked(), Nan::New(static_cast<double>(msg.manufacturer_id)));
    Nan::Set(o, Nan::New("universalNonRealtime").ToLocalChecked(), Nan::New(msg.universal_non_realtime));
    Nan::Set(o, Nan::New("universalRealtime").ToLocalChecked(), Nan::New(msg.universal_realtime));
    Nan::Set(o, Nan::New("deviceId").ToLocalChecked(), Nan::New(msg.device_id));
    Nan::Set(o, Nan::New("subId1").ToLocalChecked(), Nan::New(msg.sub_id1));
    Nan::Set(o, Nan::New("subId2").ToLocalChecked(), Nan::New(msg.sub_id2));
    Nan::Set(o, Nan::New("payload").ToLocalChecked(), U8ToJS(msg.payload));
    info.GetReturnValue().Set(o);
}

void SysexWrap::BuildIdentityRequest(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSysex(info.This());
    if (!w) return;
    info.GetReturnValue().Set(U8ToJS(w->Get().BuildIdentityRequest(
        static_cast<uint8_t>(IntArg(info, 0, 0x7F)))));
}

void SysexWrap::BuildIdentityReply(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSysex(info.This());
    if (!w) return;
    info.GetReturnValue().Set(U8ToJS(w->Get().BuildIdentityReply(
        static_cast<uint8_t>(IntArg(info, 0, 0x7F)),
        static_cast<uint32_t>(IntArg(info, 1, 0)),
        static_cast<uint16_t>(IntArg(info, 2, 0)),
        static_cast<uint16_t>(IntArg(info, 3, 0)),
        static_cast<uint32_t>(IntArg(info, 4, 0)))));
}


void InitMidiSysex(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Sysex", SysexWrap::New,
        {{"IsValidSysex", SysexWrap::IsValidSysex},
         {"Parse", SysexWrap::Parse},
         {"BuildIdentityRequest", SysexWrap::BuildIdentityRequest},
         {"BuildIdentityReply", SysexWrap::BuildIdentityReply}});
}

} // namespace xer::addon::audio::midi
