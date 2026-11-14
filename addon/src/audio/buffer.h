#pragma once

// xer.audio.Buffer — decoded audio buffer class wrapping
// XER::Audio::Core::AudioSourceBuffer.
//
//   const buf = xer.audio.Buffer.FromFile("song.mp3");
//   const buf2 = xer.audio.Buffer.FromPCM({data, sampleRate, channels});
//   const player = new xer.audio.Player();
//   player.Load(buf);   // Buffer is the ONLY accepted input
//
// `xer.audio.decode(path)` is a shorthand for Buffer.FromFile.

#include <memory>

#include "audio/audio_core/audio_source_loader.h"

#include "util.h"

namespace xer::addon::audio {

class BufferWrap : public Nan::ObjectWrap {
public:
    explicit BufferWrap(
        std::shared_ptr<XER::Audio::Core::AudioSourceBuffer> buffer);
    ~BufferWrap() override;

    std::shared_ptr<XER::Audio::Core::AudioSourceBuffer> Get() const {
        return buffer_;
    }

    // Fresh JS Buffer instance wrapping `buf` — factory return path.
    static v8::Local<v8::Object> ToJS(
        std::shared_ptr<XER::Audio::Core::AudioSourceBuffer> buf);
    // Adopt `buf` into info.This() — the `new Buffer()` ctor path.
    static void Adopt(
        const Nan::FunctionCallbackInfo<v8::Value>& info,
        std::shared_ptr<XER::Audio::Core::AudioSourceBuffer> buf);

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    // Static factories — registered on the Buffer constructor itself:
    //   xer.audio.Buffer.FromFile(path[, {sampleRate, channels}])
    //   xer.audio.Buffer.FromPCM({data, sampleRate, channels})
    static void FromFile(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void FromPCM(const Nan::FunctionCallbackInfo<v8::Value>& info);

    static void GetSampleRate(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetChannels(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFrames(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDuration(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFormat(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCodec(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBackend(
        const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    std::shared_ptr<XER::Audio::Core::AudioSourceBuffer> buffer_;
};

BufferWrap* UnwrapBuffer(v8::Local<v8::Value> v);
void InitAudioBuffer(v8::Local<v8::Object> audio_ns);

} // namespace xer::addon::audio
