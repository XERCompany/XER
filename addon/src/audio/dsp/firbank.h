#pragma once

// xer.audio.dsp.FIRBank — wraps XER::Audio::DSP::FIRFilterBank.

#include <memory>

#include "audio/dsp_algorithms/dsp_fir_filter_bank.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class FIRBankWrap : public Nan::ObjectWrap {
public:
    explicit FIRBankWrap();
    ~FIRBankWrap() override;

    XER::Audio::DSP::FIRFilterBank& Get() { return bank_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AddFilter(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessSample(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFilterCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMemoryStats(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::FIRFilterBank bank_;
};

FIRBankWrap* UnwrapFIRBank(v8::Local<v8::Value> v);
void InitDspFIRBank(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
