// xer.audio.codec.Aac — AacAdtsCodec binding.

#include "aac.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::AacAdtsCodec;

AacWrap::AacWrap() = default;
AacWrap::~AacWrap() = default;

AacWrap* UnwrapAac(v8::Local<v8::Value> v) {
    return UnwrapT<AacWrap>(v);
}

void AacWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new AacWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void AacWrap::Encode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAac(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Aac expects Float32Array");
        return;
    }
    std::vector<uint8_t> out;
    if (w->Get().Encode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(U8ToJS(out));
    }
}

void AacWrap::Decode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapAac(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Aac expects Uint8Array");
        return;
    }
    std::vector<float> out;
    if (w->Get().Decode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}


void InitCodecAac(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Aac", AacWrap::New,
        {{"Encode", AacWrap::Encode},
         {"Decode", AacWrap::Decode}});
}

} // namespace xer::addon::audio::codec
