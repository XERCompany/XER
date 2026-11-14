#pragma once

// xer.audio.midi.Sysex — wraps XER::Audio::MIDI::MidiSysexHandler.

#include <memory>

#include "audio/midi_sequencing/midi_sysex_handler.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::midi {

class SysexWrap : public Nan::ObjectWrap {
public:
    explicit SysexWrap();
    ~SysexWrap() override;

    XER::Audio::MIDI::MidiSysexHandler& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsValidSysex(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Parse(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void BuildIdentityRequest(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void BuildIdentityReply(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiSysexHandler a_;
};

SysexWrap* UnwrapSysex(v8::Local<v8::Value> v);
void InitMidiSysex(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
