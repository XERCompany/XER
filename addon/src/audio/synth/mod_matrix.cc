// xer.audio.synth.ModMatrix — SynthModulationMatrix binding.

#include "mod_matrix.h"

#include <vector>
#include <string>

namespace xer::addon::audio::synth {

using XER::Audio::Synth::SynthModulationMatrix;

ModMatrixWrap::ModMatrixWrap() = default;
ModMatrixWrap::~ModMatrixWrap() = default;

ModMatrixWrap* UnwrapModMatrix(v8::Local<v8::Value> v) {
    return UnwrapT<ModMatrixWrap>(v);
}

void ModMatrixWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ModMatrixWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ModMatrixWrap::SetAmount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    w->Get().SetAmount(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<uint32_t>(IntArg(info, 1, 0)),
        static_cast<float>(NumArg(info, 2, 0.0)));
}

void ModMatrixWrap::GetAmount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetAmount(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<uint32_t>(IntArg(info, 1, 0)))));
}

void ModMatrixWrap::SetSourceValue(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    w->Get().SetSourceValue(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)));
}

void ModMatrixWrap::GetSourceValue(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetSourceValue(
        static_cast<uint32_t>(IntArg(info, 0, 0)))));
}

void ModMatrixWrap::ConfigureDestination(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    w->Get().ConfigureDestination(
        static_cast<uint32_t>(IntArg(info, 0, 0)),
        static_cast<float>(NumArg(info, 1, 0.0)),
        static_cast<float>(NumArg(info, 2, -1e30f)),
        static_cast<float>(NumArg(info, 3, 1e30f)));
}

void ModMatrixWrap::Apply(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    float v = 0.0f;
    if (!w->Get().Apply(static_cast<uint32_t>(IntArg(info, 0, 0)), &v))
        return;
    info.GetReturnValue().Set(Nan::New(v));
}

void ModMatrixWrap::GetDestinationValue(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetDestinationValue(
        static_cast<uint32_t>(IntArg(info, 0, 0)))));
}

void ModMatrixWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapModMatrix(info.This());
    if (!w) return;
    w->Get().Reset();
}


void InitSynthModMatrix(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ModMatrix", ModMatrixWrap::New,
        {{"SetAmount", ModMatrixWrap::SetAmount},
         {"GetAmount", ModMatrixWrap::GetAmount},
         {"SetSourceValue", ModMatrixWrap::SetSourceValue},
         {"GetSourceValue", ModMatrixWrap::GetSourceValue},
         {"ConfigureDestination", ModMatrixWrap::ConfigureDestination},
         {"Apply", ModMatrixWrap::Apply},
         {"GetDestinationValue", ModMatrixWrap::GetDestinationValue},
         {"Reset", ModMatrixWrap::Reset}});
}

} // namespace xer::addon::audio::synth
