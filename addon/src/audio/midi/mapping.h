#pragma once

// xer.audio.midi.Mapping — wraps XER::Audio::MIDI::MidiControllerMapping.

#include <memory>

#include "audio/midi_sequencing/midi_controller_mapping.h"

#include "audio/audio_util.h"
#include "midi_event.h"

namespace xer::addon::audio::midi {

class MappingWrap : public Nan::ObjectWrap {
public:
    explicit MappingWrap();
    ~MappingWrap() override;

    XER::Audio::MIDI::MidiControllerMapping& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Bind(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Resolve(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiControllerMapping a_;
};

MappingWrap* UnwrapMapping(v8::Local<v8::Value> v);
void InitMidiMapping(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
