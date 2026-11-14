#pragma once

// xer.audio.codec.Mp3 — wraps XER::Audio::CodecIO::Mp3LameCodec.

#include <memory>

#include "audio/file_io_codecs/codec_mp3_lame.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class Mp3Wrap : public Nan::ObjectWrap {
public:
    explicit Mp3Wrap();
    ~Mp3Wrap() override;

    XER::Audio::CodecIO::Mp3LameCodec& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Encode(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void EncodeInterleaved(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Decode(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::Mp3LameCodec a_;
};

Mp3Wrap* UnwrapMp3(v8::Local<v8::Value> v);
void InitCodecMp3(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
