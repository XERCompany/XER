#pragma once

// xer.audio.midi.Parser — wraps XER::Audio::MIDI::MidiParser.

#include <memory>

#include "audio/midi_sequencing/midi_parser.h"

#include "audio/audio_util.h"
#include "midi_event.h"

namespace xer::addon::audio::midi {

class ParserWrap : public Nan::ObjectWrap {
public:
    explicit ParserWrap();
    ~ParserWrap() override;

    XER::Audio::MIDI::MidiParser& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Parse(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void DescribeType(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiParser a_;
};

ParserWrap* UnwrapParser(v8::Local<v8::Value> v);
void InitMidiParser(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
