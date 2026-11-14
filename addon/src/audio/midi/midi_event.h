// MIDI event <-> JS conversion shared by the midi_* bindings.
#pragma once

#include "audio/audio_util.h"
#include "audio/midi_sequencing/midi_parser.h"

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiEvent;

inline v8::Local<v8::Value> MidiEventToJS(const MidiEvent& e) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("type").ToLocalChecked(),
             Nan::New(static_cast<int>(e.type)));
    Nan::Set(o, Nan::New("status").ToLocalChecked(), Nan::New(e.status));
    Nan::Set(o, Nan::New("channel").ToLocalChecked(), Nan::New(e.channel));
    Nan::Set(o, Nan::New("data1").ToLocalChecked(), Nan::New(e.data1));
    Nan::Set(o, Nan::New("data2").ToLocalChecked(), Nan::New(e.data2));
    Nan::Set(o, Nan::New("tick").ToLocalChecked(),
             Nan::New(static_cast<double>(e.tick)));
    Nan::Set(o, Nan::New("runningStatus").ToLocalChecked(),
             Nan::New(e.running_status));
    Nan::Set(o, Nan::New("message").ToLocalChecked(), U8ToJS(e.message));
    return o;
}

inline std::vector<MidiEvent> MidiEventsFromJS(v8::Local<v8::Value> v) {
    std::vector<MidiEvent> out;
    if (v.IsEmpty()) return out;
    if (!v->IsArray()) {
        if (!v->IsObject()) return out;
        // Single event object -> one-element list.
        auto tmp = Nan::New<v8::Array>(1);
        Nan::Set(tmp, 0u, v);
        return MidiEventsFromJS(tmp);
    }
    auto arr = v.As<v8::Array>();
    const size_t n = arr->Length();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> e;
        if (!arr->Get(Nan::GetCurrentContext(), static_cast<uint32_t>(i))
                 .ToLocal(&e) ||
            !e->IsObject()) {
            continue;
        }
        v8::Local<v8::Object> o = e.As<v8::Object>();
        MidiEvent ev;
        auto get = [&](const char* k, v8::Local<v8::Value>* dst) {
            return Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(dst);
        };
        v8::Local<v8::Value> t;
        if (get("type", &t) && t->IsNumber()) {
            ev.type = static_cast<XER::Audio::MIDI::MidiMessageType>(
                static_cast<int>(t.As<v8::Number>()->Value()));
        }
        v8::Local<v8::Value> x;
        if (get("status", &x) && x->IsNumber())
            ev.status = static_cast<uint8_t>(x.As<v8::Number>()->Value());
        if (get("channel", &x) && x->IsNumber())
            ev.channel = static_cast<uint8_t>(x.As<v8::Number>()->Value());
        if (get("data1", &x) && x->IsNumber())
            ev.data1 = static_cast<uint8_t>(x.As<v8::Number>()->Value());
        if (get("data2", &x) && x->IsNumber())
            ev.data2 = static_cast<uint8_t>(x.As<v8::Number>()->Value());
        if (get("tick", &x) && x->IsNumber())
            ev.tick = static_cast<uint32_t>(x.As<v8::Number>()->Value());
        if (get("runningStatus", &x) && x->IsBoolean())
            ev.running_status = x.As<v8::Boolean>()->Value();
        if (get("message", &x)) U8FromJS(x, &ev.message);
        out.push_back(std::move(ev));
    }
    return out;
}

inline v8::Local<v8::Value> MidiEventsToJS(
    const std::vector<MidiEvent>& evs) {
    auto arr = Nan::New<v8::Array>(evs.size());
    for (size_t i = 0; i < evs.size(); ++i) {
        Nan::Set(arr, static_cast<uint32_t>(i), MidiEventToJS(evs[i]));
    }
    return arr;
}

} // namespace xer::addon::audio::midi
