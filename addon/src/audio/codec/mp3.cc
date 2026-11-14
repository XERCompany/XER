// xer.audio.codec.Mp3 — Mp3LameCodec binding.

#include "mp3.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::Mp3LameCodec;

Mp3Wrap::Mp3Wrap() = default;
Mp3Wrap::~Mp3Wrap() = default;

Mp3Wrap* UnwrapMp3(v8::Local<v8::Value> v) {
    return UnwrapT<Mp3Wrap>(v);
}

void Mp3Wrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new Mp3Wrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void Mp3Wrap::Encode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMp3(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Mp3 expects Float32Array");
        return;
    }
    std::vector<uint8_t> out;
    if (w->Get().Encode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(U8ToJS(out));
    }
}

void Mp3Wrap::EncodeInterleaved(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMp3(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Mp3 expects Float32Array");
        return;
    }
    std::vector<uint8_t> out;
    const int channels = IntArg(info, 2, 2);
    const size_t frames =
        channels > 0 ? in.size() / static_cast<size_t>(channels) : 0;
    if (w->Get().EncodeInterleaved(in.data(), frames,
            IntArg(info, 1, 44100), channels, out)) {
        info.GetReturnValue().Set(U8ToJS(out));
    }
}

void Mp3Wrap::Decode(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapMp3(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("Mp3 expects Uint8Array");
        return;
    }
    std::vector<float> out;
    if (w->Get().Decode(in.data(), in.size(), out)) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}


void InitCodecMp3(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Mp3", Mp3Wrap::New,
        {{"Encode", Mp3Wrap::Encode},
         {"EncodeInterleaved", Mp3Wrap::EncodeInterleaved},
         {"Decode", Mp3Wrap::Decode}});
}

} // namespace xer::addon::audio::codec
