// xer.audio.codec.Ogg — OggVorbisCodec binding.

#include "ogg.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::OggVorbisCodec;

OggWrap::OggWrap() = default;
OggWrap::~OggWrap() = default;

OggWrap* UnwrapOgg(v8::Local<v8::Value> v) {
    return UnwrapT<OggWrap>(v);
}

void OggWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new OggWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void OggWrap::Encode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOgg(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Ogg expects Float32Array");
        return;
    }
    std::vector<uint8_t> out;
    if (w->Get().Encode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(U8ToJS(out));
    }
}

void OggWrap::Decode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapOgg(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Ogg expects Uint8Array");
        return;
    }
    std::vector<float> out;
    if (w->Get().Decode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}


void InitCodecOgg(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Ogg", OggWrap::New,
        {{"Encode", OggWrap::Encode},
         {"Decode", OggWrap::Decode}});
}

} // namespace xer::addon::audio::codec
