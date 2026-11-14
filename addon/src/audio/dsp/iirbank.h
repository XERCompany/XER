#pragma once

// xer.audio.dsp.IIRBank — wraps XER::Audio::DSP::IIRFilterBank.

#include <memory>

#include "audio/dsp_algorithms/dsp_iir_filter_bank.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::dsp {

class IIRBankWrap : public Nan::ObjectWrap {
public:
    explicit IIRBankWrap();
    ~IIRBankWrap() override;

    XER::Audio::DSP::IIRFilterBank& Get() { return bank_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void AddBiquad(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ProcessSample(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFilterCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Clear(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::DSP::IIRFilterBank bank_;
};

IIRBankWrap* UnwrapIIRBank(v8::Local<v8::Value> v);
void InitDspIIRBank(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::dsp
