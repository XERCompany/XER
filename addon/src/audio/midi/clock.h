#pragma once

// xer.audio.midi.Clock — wraps XER::Audio::MIDI::MidiClockGenerator.

#include <memory>

#include "audio/midi_sequencing/midi_clock_generator.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::midi {

class ClockWrap : public Nan::ObjectWrap {
public:
    explicit ClockWrap();
    ~ClockWrap() override;

    XER::Audio::MIDI::MidiClockGenerator& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetTempoBpm(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AdvanceSamples(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiClockGenerator a_;
};

ClockWrap* UnwrapClock(v8::Local<v8::Value> v);
void InitMidiClock(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
