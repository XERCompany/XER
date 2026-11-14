// xer.audio.synth.MultistageEnv — SynthEnvMultistage binding.

#include "multistage.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthEnvMultistage;

MultistageWrap::MultistageWrap() = default;
MultistageWrap::~MultistageWrap() = default;

MultistageWrap* UnwrapMultistage(v8::Local<v8::Value> v) {
    return UnwrapT<MultistageWrap>(v);
}

void MultistageWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new MultistageWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void MultistageWrap::SetStages(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMultistage(info.This());
    if (!w) return;
    if (info.Length() < 1 || !info[0]->IsArray()) return;
    auto arr = info[0].As<v8::Array>();
    std::vector<XER::Audio::Synth::EnvStage> stages;
    for (size_t i = 0; i < arr->Length(); ++i) {
        v8::Local<v8::Value> e;
        if (!arr->Get(Nan::GetCurrentContext(), static_cast<uint32_t>(i))
                 .ToLocal(&e) || !e->IsObject()) continue;
        v8::Local<v8::Object> o = e.As<v8::Object>();
        v8::Local<v8::Value> t, tm;
        XER::Audio::Synth::EnvStage s{};
        if (Nan::Get(o, Nan::New("target").ToLocalChecked()).ToLocal(&t) &&
            t->IsNumber()) s.target = static_cast<float>(t.As<v8::Number>()->Value());
        if (Nan::Get(o, Nan::New("time_s").ToLocalChecked()).ToLocal(&tm) &&
            tm->IsNumber()) s.time_s = static_cast<float>(tm.As<v8::Number>()->Value());
        stages.push_back(s);
    }
    w->Get().SetStages(stages);
}

void MultistageWrap::Trigger(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMultistage(info.This());
    if (!w) return;
    w->Get().Trigger();
}

void MultistageWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMultistage(info.This());
    if (!w) { info.GetReturnValue().Set(Nan::New(0.0f)); return; }
    info.GetReturnValue().Set(Nan::New(w->Get().Process(
        static_cast<float>(NumArg(info, 0, 44100.0)))));
}


void InitSynthMultistage(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "MultistageEnv", MultistageWrap::New,
        {{"SetStages", MultistageWrap::SetStages},
         {"Trigger", MultistageWrap::Trigger},
         {"Process", MultistageWrap::Process}});
}

} // namespace xer::addon::audio::synth
