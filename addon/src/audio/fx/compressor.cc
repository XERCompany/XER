// xer.audio.fx.Compressor — dynamics compressor.

#include "compressor.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::DynamicsCompressor;

CompressorWrap::CompressorWrap() = default;
CompressorWrap::~CompressorWrap() = default;

CompressorWrap* UnwrapCompressor(v8::Local<v8::Value> v) {
    return UnwrapT<CompressorWrap>(v);
}

void CompressorWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new CompressorWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void CompressorWrap::SetSampleRate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    if (w) w->Get().SetSampleRate(
        static_cast<float>(NumArg(info, 0, 44100.0)));
}

void CompressorWrap::SetThresholdDb(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    if (w) w->Get().SetThresholdDb(
        static_cast<float>(NumArg(info, 0, -24.0)));
}

void CompressorWrap::SetRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    if (w) w->Get().SetRatio(static_cast<float>(NumArg(info, 0, 4.0)));
}

void CompressorWrap::SetAttackMs(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    if (w) w->Get().SetAttackMs(
        static_cast<float>(NumArg(info, 0, 10.0)));
}

void CompressorWrap::SetReleaseMs(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    if (w) w->Get().SetReleaseMs(
        static_cast<float>(NumArg(info, 0, 100.0)));
}

void CompressorWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    if (!w || info.Length() < 1) {
        return;
    }
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Compressor.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    const bool ok = w->Get().ProcessBlock(in.data(), in.size(), out.data());
    if (ok) { info.GetReturnValue().Set(F32ToJS(out)); }
}

void CompressorWrap::Process(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    CompressorWrap::ProcessBlock(info);
}

void CompressorWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapCompressor(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxCompressor(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Compressor", CompressorWrap::New,
        {{"SetSampleRate", CompressorWrap::SetSampleRate},
         {"SetThresholdDb", CompressorWrap::SetThresholdDb},
         {"SetRatio", CompressorWrap::SetRatio},
         {"SetAttackMs", CompressorWrap::SetAttackMs},
         {"SetReleaseMs", CompressorWrap::SetReleaseMs},
         {"ProcessBlock", CompressorWrap::ProcessBlock},
         {"Process", CompressorWrap::Process},
         {"GetReport", CompressorWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
