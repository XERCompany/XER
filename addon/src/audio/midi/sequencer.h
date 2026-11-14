#pragma once

// xer.audio.midi.Sequencer — wraps XER::Audio::MIDI::MidiPatternSequencer.

#include <memory>

#include "audio/midi_sequencing/midi_pattern_sequencer.h"

#include "audio/audio_util.h"
#include "midi_event.h"

namespace xer::addon::audio::midi {

class SequencerWrap : public Nan::ObjectWrap {
public:
    explicit SequencerWrap();
    ~SequencerWrap() override;

    XER::Audio::MIDI::MidiPatternSequencer& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetPattern(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetPattern(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetLoopLength(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void CollectRange(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiPatternSequencer a_;
    std::vector<XER::Audio::MIDI::MidiEvent> pattern_copy_;
};

SequencerWrap* UnwrapSequencer(v8::Local<v8::Value> v);
void InitMidiSequencer(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
