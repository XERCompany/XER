#pragma once

// xer.audio.codec.Id3 — wraps XER::Audio::CodecIO::MetadataId3.

#include <memory>

#include "audio/file_io_codecs/codec_metadata_id3.h"

#include "audio/audio_util.h"
#include "audio/file_io_codecs/codec_metadata_id3.h"

namespace xer::addon::audio::codec {

class Id3Wrap : public Nan::ObjectWrap {
public:
    explicit Id3Wrap();
    ~Id3Wrap() override;

    XER::Audio::CodecIO::MetadataId3& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Parse(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Build(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::CodecIO::MetadataId3 a_;
};

Id3Wrap* UnwrapId3(v8::Local<v8::Value> v);
void InitCodecId3(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::codec
