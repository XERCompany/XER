// xer.audio.midi.Dispatcher — MidiEventDispatcher binding.

#include "dispatcher.h"

#include <vector>
#include <string>

namespace xer::addon::audio::midi {

using XER::Audio::MIDI::MidiEventDispatcher;

DispatcherWrap::DispatcherWrap()
    : js_cbs_(std::make_shared<
          std::vector<std::unique_ptr<Nan::Persistent<v8::Function>>>>()) {}
DispatcherWrap::~DispatcherWrap() {
    if (js_cbs_) {
        for (auto& cb : *js_cbs_) cb->Reset();
        js_cbs_->clear();
    }
}

DispatcherWrap* UnwrapDispatcher(v8::Local<v8::Value> v) {
    return UnwrapT<DispatcherWrap>(v);
}

void DispatcherWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new DispatcherWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void DispatcherWrap::AddListener(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDispatcher(info.This());
    if (!w) return;
    if (info.Length() < 1 || !info[0]->IsFunction()) return;
    if (!w->listener_added_) {
        auto cbs = w->js_cbs_;
        w->Get().AddListener([cbs](const MidiEvent& ev) {
            v8::Isolate* iso = v8::Isolate::GetCurrent();
            v8::HandleScope hs(iso);
            for (auto& cb : *cbs) {
                if (cb->IsEmpty()) continue;
                v8::Local<v8::Value> argv[] = { MidiEventToJS(ev) };
                Nan::Call(Nan::New(*cb), Nan::GetCurrentContext()->Global(),
                          1, argv);
            }
        });
        w->listener_added_ = true;
    }
    auto slot = std::make_unique<Nan::Persistent<v8::Function>>();
    slot->Reset(info[0].As<v8::Function>());
    w->js_cbs_->push_back(std::move(slot));
}

void DispatcherWrap::Dispatch(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDispatcher(info.This());
    if (!w) return;
    if (info.Length() < 1) return;
    for (const auto& ev : MidiEventsFromJS(info[0])) w->Get().Dispatch(ev);
}

void DispatcherWrap::DispatchBatch(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapDispatcher(info.This());
    if (!w) return;
    if (info.Length() < 1) return;
    w->Get().DispatchBatch(MidiEventsFromJS(info[0]));
}


void InitMidiDispatcher(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Dispatcher", DispatcherWrap::New,
        {{"AddListener", DispatcherWrap::AddListener},
         {"Dispatch", DispatcherWrap::Dispatch},
         {"DispatchBatch", DispatcherWrap::DispatchBatch}});
}

} // namespace xer::addon::audio::midi
