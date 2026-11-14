// xer.audio.core.Latency — LatencyMonitor binding.

#include "latency.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::LatencyMonitor;

LatencyWrap::LatencyWrap() = default;
LatencyWrap::~LatencyWrap() = default;

LatencyWrap* UnwrapLatency(v8::Local<v8::Value> v) {
    return UnwrapT<LatencyWrap>(v);
}

void LatencyWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new LatencyWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void LatencyWrap::StartMonitoring(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().StartMonitoring()));
}

void LatencyWrap::StopMonitoring(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().StopMonitoring()));
}

void LatencyWrap::IsMonitoring(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsMonitoring()));
}

void LatencyWrap::RecordInputTimestamp(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    if (!w) return;
    w->Get().RecordInputTimestamp();
}

void LatencyWrap::RecordOutputTimestamp(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    if (!w) return;
    w->Get().RecordOutputTimestamp();
}

void LatencyWrap::CalculateLatency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    if (!w) return;
    const auto& l = w->Get().CalculateLatency();
    v8::Local<v8::Object> o = Nan::New<v8::Object>();
    Nan::Set(o, Nan::New("inputLatencyMs").ToLocalChecked(), Nan::New(l.input_latency_ms));
    Nan::Set(o, Nan::New("outputLatencyMs").ToLocalChecked(), Nan::New(l.output_latency_ms));
    Nan::Set(o, Nan::New("totalRoundTripMs").ToLocalChecked(), Nan::New(l.total_round_trip_ms));
    Nan::Set(o, Nan::New("processingLatencyMs").ToLocalChecked(), Nan::New(l.processing_latency_ms));
    info.GetReturnValue().Set(o);
}

void LatencyWrap::GetLatencyHistory(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    if (!w) return;
    info.GetReturnValue().Set(D64ToJS(w->Get().GetLatencyHistory()));
}

void LatencyWrap::GetAverageLatency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetAverageLatency()));
}

void LatencyWrap::GetMaxLatency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetMaxLatency()));
}

void LatencyWrap::GetMinLatency(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetMinLatency()));
}

void LatencyWrap::GetLatencyReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapLatency(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().GetLatencyReport()).ToLocalChecked());
}


void InitCoreLatency(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Latency", LatencyWrap::New,
        {{"StartMonitoring", LatencyWrap::StartMonitoring},
         {"StopMonitoring", LatencyWrap::StopMonitoring},
         {"IsMonitoring", LatencyWrap::IsMonitoring},
         {"RecordInputTimestamp", LatencyWrap::RecordInputTimestamp},
         {"RecordOutputTimestamp", LatencyWrap::RecordOutputTimestamp},
         {"CalculateLatency", LatencyWrap::CalculateLatency},
         {"GetLatencyHistory", LatencyWrap::GetLatencyHistory},
         {"GetAverageLatency", LatencyWrap::GetAverageLatency},
         {"GetMaxLatency", LatencyWrap::GetMaxLatency},
         {"GetMinLatency", LatencyWrap::GetMinLatency},
         {"GetLatencyReport", LatencyWrap::GetLatencyReport}});
}

} // namespace xer::addon::audio::core
