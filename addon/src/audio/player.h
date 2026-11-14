#pragma once

// xer.audio.Player — WASAPI playback class wrapping
// XER::Content::Audio::AudioPlayer.
//
//   const player = new xer.audio.Player();
//   player.Initialize(44100, 2);
//   player.Load(xer.audio.Buffer.FromFile("song.mp3"));  // Buffer ONLY
//   player.SetOnState(s => console.log(s));
//   player.Play();
//
// Load() accepts ONLY xer.audio.Buffer instances — file decoding lives in
// xer.audio.Buffer / xer.audio.decode().

#include <memory>

#include "content/audio/audio_player.h"

#include "callback.h"
#include "util.h"

// windows.h (via audio_player.h) function-like-macro defines GetCurrentTime,
// which mangles the member declarations below — undef before use.
#undef GetCurrentTime

namespace xer::addon::audio {

class PlayerWrap : public Nan::ObjectWrap {
public:
    explicit PlayerWrap(
        std::shared_ptr<XER::Content::Audio::AudioPlayer> player);
    ~PlayerWrap() override;

    std::shared_ptr<XER::Content::Audio::AudioPlayer> Get() const {
        return player_;
    }
    std::shared_ptr<CallbackStore> Callbacks() const {
        return callbacks_;
    }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsInitialized(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Load(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsLoaded(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Play(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Pause(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Stop(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Seek(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetState(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCurrentTime(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDuration(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetVolume(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetVolume(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetSampleRate(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetChannelCount(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetFadeInOut(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetOnPosition(
        const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SetOnState(
        const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    std::shared_ptr<XER::Content::Audio::AudioPlayer> player_;
    std::shared_ptr<CallbackStore> callbacks_;
};

PlayerWrap* UnwrapPlayer(v8::Local<v8::Value> v);
void InitAudioPlayer(v8::Local<v8::Object> audio_ns);

} // namespace xer::addon::audio
