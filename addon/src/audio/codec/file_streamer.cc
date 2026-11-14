// xer.audio.codec.FileStreamer — CodecFileStreamer binding.

#include "file_streamer.h"

#include <vector>
#include <string>

namespace xer::addon::audio::codec {

using XER::Audio::CodecIO::CodecFileStreamer;

StreamerWrap::StreamerWrap() = default;
StreamerWrap::~StreamerWrap() = default;

StreamerWrap* UnwrapStreamer(v8::Local<v8::Value> v) {
    return UnwrapT<StreamerWrap>(v);
}

void StreamerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new StreamerWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void StreamerWrap::OpenRead(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapStreamer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().OpenRead(StrArg(info, 0))));
}

void StreamerWrap::OpenWrite(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapStreamer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        w->Get().OpenWrite(StrArg(info, 0))));
}

void StreamerWrap::ReadBytes(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapStreamer(info.This());
    if (!w) return;
    const size_t n = static_cast<size_t>(IntArg(info, 0, 4096));
    std::vector<uint8_t> buf(n);
    const size_t got = w->Get().ReadBytes(buf.data(), n);
    buf.resize(got);
    info.GetReturnValue().Set(U8ToJS(buf));
}

void StreamerWrap::WriteBytes(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapStreamer(info.This());
    if (!w) return;
    std::vector<uint8_t> in;
    if (!U8FromJS(info[0], &in)) {
        Nan::ThrowError("FileStreamer.WriteBytes expects Uint8Array");
        return;
    }
    info.GetReturnValue().Set(Nan::New(
        static_cast<double>(w->Get().WriteBytes(in.data(), in.size()))));
}

void StreamerWrap::Close(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapStreamer(info.This());
    if (!w) return;
    w->Get().Close();
}


void InitCodecStreamer(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "FileStreamer", StreamerWrap::New,
        {{"OpenRead", StreamerWrap::OpenRead},
         {"OpenWrite", StreamerWrap::OpenWrite},
         {"ReadBytes", StreamerWrap::ReadBytes},
         {"WriteBytes", StreamerWrap::WriteBytes},
         {"Close", StreamerWrap::Close}});
}

} // namespace xer::addon::audio::codec
