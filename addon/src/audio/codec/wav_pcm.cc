// xer.audio.codec.WavPcm — WavPcmCodec binding.

#include "wav_pcm.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::WavPcmCodec;

WavPcmWrap::WavPcmWrap() = default;
WavPcmWrap::~WavPcmWrap() = default;

WavPcmWrap* UnwrapWavPcm(v8::Local<v8::Value> v) {
    return UnwrapT<WavPcmWrap>(v);
}

void WavPcmWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new WavPcmWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void WavPcmWrap::Encode16(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapWavPcm(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("WavPcm expects Float32Array");
        return;
    }
    std::vector<uint8_t> out;
    const int channels = IntArg(info, 2, 1);
    const size_t frames =
        channels > 0 ? in.size() / static_cast<size_t>(channels) : 0;
    if (w->Get().Encode16(in.data(), frames, out,
            IntArg(info, 1, 44100), channels)) {
        info.GetReturnValue().Set(U8ToJS(out));
    }
}

void WavPcmWrap::Decode16(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapWavPcm(info.This());
    if (!w) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("WavPcm.Decode16 expects Uint8Array");
        return;
    }
    std::vector<float> out;
    int rate = 0, ch = 0;
    if (!w->Get().Decode16(in.data(), in.size(), out, &rate, &ch)) return;
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("data").ToLocalChecked(), F32ToJS(out));
    Nan::Set(o, Nan::New("sampleRate").ToLocalChecked(), Nan::New(rate));
    Nan::Set(o, Nan::New("channels").ToLocalChecked(), Nan::New(ch));
    info.GetReturnValue().Set(o);
}


void InitCodecWavPcm(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "WavPcm", WavPcmWrap::New,
        {{"Encode16", WavPcmWrap::Encode16},
         {"Decode16", WavPcmWrap::Decode16}});
}

} // namespace xer::addon::audio::codec
