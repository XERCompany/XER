#pragma once

// xer.audio.codec.WavPcm — wraps XER::Audio::CodecIO::WavPcmCodec.

#include <memory>

#include "audio/file_io_codecs/codec_wav_pcm.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::codec {

class WavPcmWrap : public Nan::ObjectWrap {
public:
    explicit WavPcmWrap();
    ~WavPcmWrap() override;

    XER::Audio::CodecIO::WavPcmCodec& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Encode16(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Decode16(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::WavPcmCodec a_;
};

WavPcmWrap* UnwrapWavPcm(v8::Local<v8::Value> v);
void InitCodecWavPcm(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
