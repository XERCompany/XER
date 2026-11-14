#pragma once

// xer.audio.codec.Aac — wraps XER::Audio::CodecIO::AacAdtsCodec.

#include <memory>

#include "audio/file_io_codecs/codec_aac_adts.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class AacWrap : public Nan::ObjectWrap {
public:
    explicit AacWrap();
    ~AacWrap() override;

    XER::Audio::CodecIO::AacAdtsCodec& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Encode(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Decode(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::AacAdtsCodec a_;
};

AacWrap* UnwrapAac(v8::Local<v8::Value> v);
void InitCodecAac(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
