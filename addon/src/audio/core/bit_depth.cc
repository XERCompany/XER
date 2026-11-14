// xer.audio.core.BitDepth — AudioBitDepthConverter binding.

#include "bit_depth.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioBitDepthConverter;

BitDepthWrap::BitDepthWrap() = default;
BitDepthWrap::~BitDepthWrap() = default;

BitDepthWrap* UnwrapBitDepth(v8::Local<v8::Value> v) {
    return UnwrapT<BitDepthWrap>(v);
}

void BitDepthWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new BitDepthWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void BitDepthWrap::GetBytesPerSample(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitDepth(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(XER::Audio::Core::AudioBitDepthConverter::GetBytesPerSample(static_cast<XER::Audio::Core::AudioBitDepth>(IntArg(info, 0, 3))))));
}

void BitDepthWrap::IsFloatingPoint(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitDepth(info.This());
    info.GetReturnValue().Set(Nan::New(XER::Audio::Core::AudioBitDepthConverter::IsFloatingPoint(static_cast<XER::Audio::Core::AudioBitDepth>(IntArg(info, 0, 5)))));
}

void BitDepthWrap::IsSupported(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitDepth(info.This());
    info.GetReturnValue().Set(Nan::New(XER::Audio::Core::AudioBitDepthConverter::IsSupported(static_cast<XER::Audio::Core::AudioBitDepth>(IntArg(info, 0, 3)))));
}

void BitDepthWrap::NormalizeSamples(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitDepth(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("BitDepth.NormalizeSamples expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(Nan::New(w->Get().NormalizeSamples(
        in.data(), in.size(), static_cast<float>(NumArg(info, 1, 1.0)))));
}


void InitCoreBitDepth(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "BitDepth", BitDepthWrap::New,
        {{"GetBytesPerSample", BitDepthWrap::GetBytesPerSample},
         {"IsFloatingPoint", BitDepthWrap::IsFloatingPoint},
         {"IsSupported", BitDepthWrap::IsSupported},
         {"NormalizeSamples", BitDepthWrap::NormalizeSamples}});
}

} // namespace xer::addon::audio::core
