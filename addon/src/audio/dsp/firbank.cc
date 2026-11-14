// xer.audio.dsp.FIRBank — parallel FIR filter bank.

#include "firbank.h"

#include <vector>

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::FIRFilterBank;

FIRBankWrap::FIRBankWrap() = default;
FIRBankWrap::~FIRBankWrap() = default;

FIRBankWrap* UnwrapFIRBank(v8::Local<v8::Value> v) {
    return UnwrapT<FIRBankWrap>(v);
}

void FIRBankWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new FIRBankWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FIRBankWrap::AddFilter(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFIRBank(info.This());
    if (!w || info.Length() < 1) {
        info.GetReturnValue().Set(Nan::New(false));
        return;
    }
    std::vector<float> taps;
    if (!F32FromJS(info[0], &taps)) {
        Nan::ThrowError("FIRBank.AddFilter expects Float32Array of taps");
        return;
    }
    info.GetReturnValue().Set(Nan::New(w->Get().AddFilter(taps)));
}

void FIRBankWrap::ProcessSample(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFIRBank(info.This());
    if (!w) return;
    const float x = static_cast<float>(NumArg(info, 0));
    std::vector<float> outputs(w->Get().GetFilterCount(), 0.0f);
    const bool ok = w->Get().ProcessSample(x, outputs);
    if (ok) { info.GetReturnValue().Set(F32ToJS(outputs)); }
}

void FIRBankWrap::GetFilterCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFIRBank(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? static_cast<double>(w->Get().GetFilterCount()) : 0.0));
}

void FIRBankWrap::Clear(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFIRBank(info.This());
    if (w) w->Get().Clear();
}

void FIRBankWrap::GetMemoryStats(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFIRBank(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetMemoryStats() : std::string()).ToLocalChecked());
}

void FIRBankWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFIRBank(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitDspFIRBank(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "FIRBank", FIRBankWrap::New,
        {{"AddFilter", FIRBankWrap::AddFilter},
         {"ProcessSample", FIRBankWrap::ProcessSample},
         {"GetFilterCount", FIRBankWrap::GetFilterCount},
         {"Clear", FIRBankWrap::Clear},
         {"GetMemoryStats", FIRBankWrap::GetMemoryStats},
         {"GetReport", FIRBankWrap::GetReport}});
}

} // namespace xer::addon::audio::dsp
