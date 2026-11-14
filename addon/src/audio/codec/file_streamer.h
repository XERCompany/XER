#pragma once

// xer.audio.codec.FileStreamer — wraps XER::Audio::CodecIO::CodecFileStreamer.

#include <memory>

#include "audio/file_io_codecs/codec_file_streamer.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class StreamerWrap : public Nan::ObjectWrap {
public:
    explicit StreamerWrap();
    ~StreamerWrap() override;

    XER::Audio::CodecIO::CodecFileStreamer& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void OpenRead(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void OpenWrite(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ReadBytes(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void WriteBytes(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Close(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::CodecFileStreamer a_;
};

StreamerWrap* UnwrapStreamer(v8::Local<v8::Value> v);
void InitCodecStreamer(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
