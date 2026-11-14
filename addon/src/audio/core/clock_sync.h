#pragma once

// xer.audio.core.ClockSync — wraps XER::Audio::Core::AudioClockSync.

#include <memory>

#include "audio/audio_core/audio_clock_sync.h"

#include "audio/audio_util.h"

// Win32 GetCurrentTime() macro would rename this method.
#undef GetCurrentTime

namespace xer::addon::audio::core {

class ClockSyncWrap : public Nan::ObjectWrap {
public:
    explicit ClockSyncWrap();
    ~ClockSyncWrap() override;

    XER::Audio::Core::AudioClockSync& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Initialize(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsInitialized(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void UpdateClock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void SyncToExternal(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetClockInfo(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCurrentFrame(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetCurrentTime(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetDriftPercentage(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void CheckClockLock(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void CalculateSampleRateRatio(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::AudioClockSync a_;
};

ClockSyncWrap* UnwrapClockSync(v8::Local<v8::Value> v);
void InitCoreClockSync(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
