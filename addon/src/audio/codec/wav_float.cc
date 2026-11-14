// xer.audio.codec.WavFloat — WavFloatCodec binding.

#include "wav_float.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::WavFloatCodec;

WavFloatWrap::WavFloatWrap() = default;
WavFloatWrap::~WavFloatWrap() = default;

WavFloatWrap* UnwrapWavFloat(v8::Local<v8::Value> v) {
    return UnwrapT<WavFloatWrap>(v);
}

void WavFloatWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new WavFloatWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void WavFloatWrap::Encode32(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapWavFloat(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("WavFloat expects Float32Array");
        return;
    }
    std::vector<uint8_t> out;
    if (w->Get().Encode32(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(U8ToJS(out));
    }
}

void WavFloatWrap::Decode32(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapWavFloat(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("WavFloat expects Uint8Array");
        return;
    }
    std::vector<float> out;
    if (w->Get().Decode32(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}


void InitCodecWavFloat(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "WavFloat", WavFloatWrap::New,
        {{"Encode32", WavFloatWrap::Encode32},
         {"Decode32", WavFloatWrap::Decode32}});
}

} // namespace xer::addon::audio::codec
