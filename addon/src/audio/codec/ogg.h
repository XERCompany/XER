#pragma once

// xer.audio.codec.Ogg — wraps XER::Audio::CodecIO::OggVorbisCodec.

#include <memory>

#include "audio/file_io_codecs/codec_ogg_vorbis.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class OggWrap : public Nan::ObjectWrap {
public:
    explicit OggWrap();
    ~OggWrap() override;

    XER::Audio::CodecIO::OggVorbisCodec& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Encode(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Decode(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::OggVorbisCodec a_;
};

OggWrap* UnwrapOgg(v8::Local<v8::Value> v);
void InitCodecOgg(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
