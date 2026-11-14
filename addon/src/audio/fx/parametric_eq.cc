// xer.audio.fx.ParametricEQ — parametric equalizer.
// SetBand(index, {frequency, q, gainDb, enabled?}); GetBand(index) -> obj.

#include "parametric_eq.h"

#include <vector>

#include "convert.h"

namespace xer::addon::audio::fx {

using XER::Audio::FX::ParametricBand;
using XER::Audio::FX::ParametricEQ;

ParametricEQWrap::ParametricEQWrap() = default;
ParametricEQWrap::~ParametricEQWrap() = default;

ParametricEQWrap* UnwrapParametricEQ(v8::Local<v8::Value> v) {
    return UnwrapT<ParametricEQWrap>(v);
}

namespace {

ParametricBand BandFromJS(v8::Local<v8::Value> v) {
    ParametricBand b{};
    if (v.IsEmpty() || !v->IsObject()) return b;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    b.frequency = static_cast<float>(
        xer::addon::NumProp(o, "frequency", b.frequency));
    b.q = static_cast<float>(xer::addon::NumProp(o, "q", b.q));
    b.gain_db = static_cast<float>(xer::addon::NumProp(o, "gainDb", b.gain_db));
    b.enabled = xer::addon::BoolProp(o, "enabled", b.enabled);
    return b;
}

v8::Local<v8::Value> BandToJS(const ParametricBand& b) {
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("frequency").ToLocalChecked(), Nan::New(b.frequency));
    Nan::Set(o, Nan::New("q").ToLocalChecked(), Nan::New(b.q));
    Nan::Set(o, Nan::New("gainDb").ToLocalChecked(), Nan::New(b.gain_db));
    Nan::Set(o, Nan::New("enabled").ToLocalChecked(), Nan::New(b.enabled));
    return o;
}

} // namespace

void ParametricEQWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ParametricEQWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ParametricEQWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParametricEQ(info.This());
    if (!w) return;
    const float rate = static_cast<float>(NumArg(info, 0, 44100.0));
    const size_t bands = static_cast<size_t>(IntArg(info, 1, 4));
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(rate, bands)));
}

void ParametricEQWrap::SetBand(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParametricEQ(info.This());
    if (!w || info.Length() < 2) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    const size_t idx = static_cast<size_t>(IntArg(info, 0));
    info.GetReturnValue().Set(
        Nan::New(w->Get().SetBand(idx, BandFromJS(info[1]))));
}

void ParametricEQWrap::GetBand(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParametricEQ(info.This());
    if (!w) return;
    const size_t idx = static_cast<size_t>(IntArg(info, 0));
    info.GetReturnValue().Set(BandToJS(w->Get().GetBand(idx)));
}

void ParametricEQWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParametricEQ(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("ParametricEQ.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok = w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void ParametricEQWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    ParametricEQWrap::ProcessBlock(info);
}

void ParametricEQWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParametricEQ(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void ParametricEQWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapParametricEQ(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxParametricEQ(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ParametricEQ", ParametricEQWrap::New,
        {{"Initialize", ParametricEQWrap::Initialize},
         {"SetBand", ParametricEQWrap::SetBand},
         {"GetBand", ParametricEQWrap::GetBand},
         {"ProcessBlock", ParametricEQWrap::ProcessBlock},
         {"Process", ParametricEQWrap::Process},
         {"GetMemoryStats", ParametricEQWrap::GetMemoryStats},
         {"GetReport", ParametricEQWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
