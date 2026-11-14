// xer.audio.core.Fifo — AudioFIFOQueue binding.

#include "fifo.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioFIFOQueue;

FifoWrap::FifoWrap(size_t capacity) : a_(capacity) {}
FifoWrap::~FifoWrap() = default;

FifoWrap* UnwrapFifo(v8::Local<v8::Value> v) {
    return UnwrapT<FifoWrap>(v);
}

void FifoWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new FifoWrap(static_cast<size_t>(IntArg(info, 0, 4096)));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void FifoWrap::Write(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Fifo.Write expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(Nan::New(static_cast<double>(
        w->Get().Write(in.data(), in.size()))));
}

void FifoWrap::Read(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    if (!w) return;
    const size_t n = static_cast<size_t>(IntArg(info, 0, 512));
    std::vector<float> out(n, 0.0f);
    const size_t got = w->Get().Read(out.data(), n);
    out.resize(got);
    info.GetReturnValue().Set(F32ToJS(out));
}

void FifoWrap::GetAvailableFrames(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetAvailableFrames())));
}

void FifoWrap::GetCapacity(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetCapacity())));
}

void FifoWrap::IsEmpty(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsEmpty()));
}

void FifoWrap::IsFull(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    info.GetReturnValue().Set(Nan::New(w->Get().IsFull()));
}

void FifoWrap::Clear(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    if (!w) return;
    w->Get().Clear();
}

void FifoWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    if (!w) return;
    w->Get().Reset();
}

void FifoWrap::GetTotalWritten(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        static_cast<double>(w->Get().GetTotalWritten())));
}

void FifoWrap::GetTotalRead(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapFifo(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(
        static_cast<double>(w->Get().GetTotalRead())));
}


void InitCoreFifo(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Fifo", FifoWrap::New,
        {{"Write", FifoWrap::Write},
         {"Read", FifoWrap::Read},
         {"GetAvailableFrames", FifoWrap::GetAvailableFrames},
         {"GetCapacity", FifoWrap::GetCapacity},
         {"IsEmpty", FifoWrap::IsEmpty},
         {"IsFull", FifoWrap::IsFull},
         {"Clear", FifoWrap::Clear},
         {"Reset", FifoWrap::Reset},
         {"GetTotalWritten", FifoWrap::GetTotalWritten},
         {"GetTotalRead", FifoWrap::GetTotalRead}});
}

} // namespace xer::addon::audio::core
