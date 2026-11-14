// xer.audio.codec.Flac — FlacDecoder binding.

#include "flac.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::FlacDecoder;

FlacWrap::FlacWrap() = default;
FlacWrap::~FlacWrap() = default;

FlacWrap* UnwrapFlac(v8::Local<v8::Value> v) {
    return UnwrapT<FlacWrap>(v);
}

void FlacWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new FlacWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FlacWrap::Decode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFlac(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Flac expects Uint8Array");
        return;
    }
    std::vector<float> out;
    if (w->Get().Decode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}


void InitCodecFlac(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Flac", FlacWrap::New,
        {{"Decode", FlacWrap::Decode}});
}

} // namespace xer::addon::audio::codec
