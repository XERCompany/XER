#pragma once

// xer.audio.midi.Dispatcher — wraps XER::Audio::MIDI::MidiEventDispatcher.

#include <memory>

#include "audio/midi_sequencing/midi_event_dispatcher.h"

#include "audio/audio_util.h"
#include "midi_event.h"
#include <memory>

namespace xer::addon::audio::midi {

class DispatcherWrap : public Nan::ObjectWrap {
public:
    explicit DispatcherWrap();
    ~DispatcherWrap() override;

    XER::Audio::MIDI::MidiEventDispatcher& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AddListener(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Dispatch(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void DispatchBatch(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::MIDI::MidiEventDispatcher a_;
    std::shared_ptr<std::vector<std::unique_ptr<Nan::Persistent<v8::Function>>>> js_cbs_;
    bool listener_added_ = false;};

DispatcherWrap* UnwrapDispatcher(v8::Local<v8::Value> v);
void InitMidiDispatcher(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::midi
