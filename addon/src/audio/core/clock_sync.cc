// xer.audio.core.ClockSync — AudioClockSync binding.

#include "clock_sync.h"

#undef GetCurrentTime

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioClockSync;

ClockSyncWrap::ClockSyncWrap() = default;
ClockSyncWrap::~ClockSyncWrap() = default;

ClockSyncWrap* UnwrapClockSync(v8::Local<v8::Value> v) {
    return UnwrapT<ClockSyncWrap>(v);
}

void ClockSyncWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new ClockSyncWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void ClockSyncWrap::Initialize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().Initialize(
        IntArg(info, 0, 44100),
        static_cast<XER::Audio::Core::ClockSource>(IntArg(info, 1, 0)))));
}

void ClockSyncWrap::IsInitialized(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsInitialized()));
}

void ClockSyncWrap::UpdateClock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    if (!w) return;
    w->Get().UpdateClock(static_cast<int64_t>(NumArg(info, 0, 0.0)));
}

void ClockSyncWrap::SyncToExternal(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().SyncToExternal(
        static_cast<int64_t>(NumArg(info, 0, 0.0)),
        static_cast<int64_t>(NumArg(info, 1, 0.0)))));
}

void ClockSyncWrap::GetClockInfo(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    if (!w) return;
    const auto& c = w->Get().GetClockInfo();
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("source").ToLocalChecked(), Nan::New(static_cast<int>(c.source)));
    Nan::Set(o, Nan::New("sampleRate").ToLocalChecked(), Nan::New(c.sample_rate));
    Nan::Set(o, Nan::New("framePosition").ToLocalChecked(), Nan::New(static_cast<double>(c.frame_position)));
    Nan::Set(o, Nan::New("timestampNs").ToLocalChecked(), Nan::New(static_cast<double>(c.timestamp_ns)));
    Nan::Set(o, Nan::New("isLocked").ToLocalChecked(), Nan::New(c.is_locked));
    info.GetReturnValue().Set(o);
}

void ClockSyncWrap::GetCurrentFrame(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        static_cast<double>(w->Get().GetCurrentFrame())));
}

void ClockSyncWrap::GetCurrentTime(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetCurrentTime()));
}

void ClockSyncWrap::GetDriftPercentage(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetDriftPercentage()));
}

void ClockSyncWrap::CheckClockLock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().CheckClockLock()));
}

void ClockSyncWrap::CalculateSampleRateRatio(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapClockSync(info.This());
    if (!w) return;
    double ratio = 0.0;
    if (!w->Get().CalculateSampleRateRatio(IntArg(info, 0, 44100),
                                           IntArg(info, 1, 48000), ratio))
        return;
    info.GetReturnValue().Set(Nan::New(ratio));
}


void InitCoreClockSync(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "ClockSync", ClockSyncWrap::New,
        {{"Initialize", ClockSyncWrap::Initialize},
         {"IsInitialized", ClockSyncWrap::IsInitialized},
         {"UpdateClock", ClockSyncWrap::UpdateClock},
         {"SyncToExternal", ClockSyncWrap::SyncToExternal},
         {"GetClockInfo", ClockSyncWrap::GetClockInfo},
         {"GetCurrentFrame", ClockSyncWrap::GetCurrentFrame},
         {"GetCurrentTime", ClockSyncWrap::GetCurrentTime},
         {"GetDriftPercentage", ClockSyncWrap::GetDriftPercentage},
         {"CheckClockLock", ClockSyncWrap::CheckClockLock},
         {"CalculateSampleRateRatio", ClockSyncWrap::CalculateSampleRateRatio},
         });
}

} // namespace xer::addon::audio::core
