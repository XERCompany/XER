#pragma once

// xer.audio.codec.Flac — wraps XER::Audio::CodecIO::FlacDecoder.

#include <memory>

#include "audio/file_io_codecs/codec_flac_decoder.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class FlacWrap : public Nan::ObjectWrap {
public:
    explicit FlacWrap();
    ~FlacWrap() override;

    XER::Audio::CodecIO::FlacDecoder& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Decode(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::FlacDecoder a_;
};

FlacWrap* UnwrapFlac(v8::Local<v8::Value> v);
void InitCodecFlac(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
