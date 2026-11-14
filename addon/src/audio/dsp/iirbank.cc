// xer.audio.dsp.IIRBank — parallel IIR (biquad) filter bank.

#include "iirbank.h"

#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::BiquadType;
using XER::Audio::DSP::IIRFilterBank;

IIRBankWrap::IIRBankWrap() = default;
IIRBankWrap::~IIRBankWrap() = default;

IIRBankWrap* UnwrapIIRBank(v8::Local<v8::Value> v) {
    return UnwrapT<IIRBankWrap>(v);
}

void IIRBankWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new IIRBankWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void IIRBankWrap::AddBiquad(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapIIRBank(info.This());
    if (!w) return;
    const auto type = EnumArg(info, 0, BiquadType::LowPass);
    const float rate = static_cast<float>(NumArg(info, 1, 44100.0));
    const float freq = static_cast<float>(NumArg(info, 2, 1000.0));
    const float q = static_cast<float>(NumArg(info, 3, 0.707f));
    info.GetReturnValue().Set(Nan::New(
        w->Get().AddBiquad(type, rate, freq, q)));
}

void IIRBankWrap::ProcessSample(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapIIRBank(info.This());
    if (!w) return;
    const float x = static_cast<float>(NumArg(info, 0));
    std::vector<float> outputs(w->Get().GetFilterCount(), 0.0f);
    const bool ok = w->Get().ProcessSample(x, outputs);
    if (ok) { info.GetReturnValue().Set(F32ToJS(outputs)); }
}

void IIRBankWrap::GetFilterCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapIIRBank(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetFilterCount()) : 0.0));
}

void IIRBankWrap::Clear(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapIIRBank(info.This());
    if (w) w->Get().Clear();
}

void IIRBankWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapIIRBank(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspIIRBank(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "IIRBank", IIRBankWrap::New,
        {{"AddBiquad", IIRBankWrap::AddBiquad},
         {"ProcessSample", IIRBankWrap::ProcessSample},
         {"GetFilterCount", IIRBankWrap::GetFilterCount},
         {"Clear", IIRBankWrap::Clear},
         {"GetReport", IIRBankWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
