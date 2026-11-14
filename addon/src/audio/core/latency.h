#pragma once

// xer.audio.core.Latency — wraps XER::Audio::Core::LatencyMonitor.

#include <memory>

#include "audio/audio_core/audio_latency_monitor.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::core {

class LatencyWrap : public Nan::ObjectWrap {
public:
    explicit LatencyWrap();
    ~LatencyWrap() override;

    XER::Audio::Core::LatencyMonitor& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void StartMonitoring(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void StopMonitoring(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void IsMonitoring(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void RecordInputTimestamp(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void RecordOutputTimestamp(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void CalculateLatency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLatencyHistory(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetAverageLatency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMaxLatency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMinLatency(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetLatencyReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::Core::LatencyMonitor a_;
};

LatencyWrap* UnwrapLatency(v8::Local<v8::Value> v);
void InitCoreLatency(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::core
