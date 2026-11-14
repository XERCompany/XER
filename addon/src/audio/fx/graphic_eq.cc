// xer.audio.fx.GraphicEQ — graphic equalizer.
// Initialize(sampleRate, [centerFreqHz, ...]); SetBandGainDb(index, gainDb).

#include "graphic_eq.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::GraphicEQ;

GraphicEQWrap::GraphicEQWrap() = default;
GraphicEQWrap::~GraphicEQWrap() = default;

GraphicEQWrap* UnwrapGraphicEQ(v8::Local<v8::Value> v) {
    return UnwrapT<GraphicEQWrap>(v);
}

void GraphicEQWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new GraphicEQWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void GraphicEQWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGraphicEQ(info.This());
    if (!w) return;
    const float rate = static_cast<float>(NumArg(info, 0, 44100.0));
    std::vector<float> freqs;
    if (info.Length() > 1 && !F32FromJS(info[1], &freqs)) {
        Nan::ThrowError("GraphicEQ.Initialize expects (rate, Float32Array)");
        return;
    }
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(rate, freqs)));
}

void GraphicEQWrap::SetBandGainDb(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGraphicEQ(info.This());
    if (!w) return;
    const size_t band = static_cast<size_t>(IntArg(info, 0));
    const float gain = static_cast<float>(NumArg(info, 1));
    w->Get().SetBandGainDb(band, gain);
}

void GraphicEQWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGraphicEQ(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("GraphicEQ.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok = w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void GraphicEQWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    GraphicEQWrap::ProcessBlock(info);
}

void GraphicEQWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapGraphicEQ(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxGraphicEQ(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "GraphicEQ", GraphicEQWrap::New,
        {{"Initialize", GraphicEQWrap::Initialize},
         {"SetBandGainDb", GraphicEQWrap::SetBandGainDb},
         {"ProcessBlock", GraphicEQWrap::ProcessBlock},
         {"Process", GraphicEQWrap::Process},
         {"GetReport", GraphicEQWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
