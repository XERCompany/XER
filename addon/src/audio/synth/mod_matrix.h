#pragma once

// xer.audio.synth.ModMatrix — wraps XER::Audio::Synth::SynthModulationMatrix.

#include <memory>

#include "audio/synth_engine/synth_modulation_matrix.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::synth {

class ModMatrixWrap : public Nan::ObjectWrap {
public:
    explicit ModMatrixWrap();
    ~ModMatrixWrap() override;

    XER::Audio::Synth::SynthModulationMatrix& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetAmount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAmount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetSourceValue(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSourceValue(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ConfigureDestination(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Apply(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDestinationValue(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Synth::SynthModulationMatrix a_;
};

ModMatrixWrap* UnwrapModMatrix(v8::Local<v8::Value> v);
void InitSynthModMatrix(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::synth
