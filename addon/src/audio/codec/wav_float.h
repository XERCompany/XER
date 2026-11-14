#pragma once

// xer.audio.codec.WavFloat — wraps XER::Audio::CodecIO::WavFloatCodec.

#include <memory>

#include "audio/file_io_codecs/codec_wav_float.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class WavFloatWrap : public Nan::ObjectWrap {
public:
    explicit WavFloatWrap();
    ~WavFloatWrap() override;

    XER::Audio::CodecIO::WavFloatCodec& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Encode32(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Decode32(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::WavFloatCodec a_;
};

WavFloatWrap* UnwrapWavFloat(v8::Local<v8::Value> v);
void InitCodecWavFloat(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
