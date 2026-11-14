#pragma once

// xer.audio.midi.Automation — wraps XER::Audio::MIDI::MidiAutomationCurve.

#include <memory>

#include "audio/midi_sequencing/midi_automation_curve.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::midi {

class AutomationWrap : public Nan::ObjectWrap {
public:
    explicit AutomationWrap();
    ~AutomationWrap() override;

    XER::Audio::MIDI::MidiAutomationCurve& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AddPoint(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Evaluate(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiAutomationCurve a_;
};

AutomationWrap* UnwrapAutomation(v8::Local<v8::Value> v);
void InitMidiAutomation(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
