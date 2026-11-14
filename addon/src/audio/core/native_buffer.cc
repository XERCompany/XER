// xer.audio.core.NativeBuffer — AudioBuffer binding.

#include "native_buffer.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioBuffer;

namespace {
v8::Global<v8::FunctionTemplate>& NativeBufferTemplateStore() {
    static auto* tpl = new v8::Global<v8::FunctionTemplate>();
    return *tpl;
}
} // namespace

NativeBufferWrap::NativeBufferWrap(int sample_rate, int channels,
                           int bit_depth, int layout) {
    XER::Audio::Core::AudioFrameInfo info{};
    info.sample_rate = sample_rate;
    info.channel_count = channels;
    info.bit_depth = bit_depth;
    info.layout = static_cast<XER::Audio::Core::AudioLayout>(layout);
    a_ = std::make_shared<XER::Audio::Core::AudioBuffer>(info, 0);
}
NativeBufferWrap::~NativeBufferWrap() = default;

v8::Local<v8::Object> NativeBufferWrap::Adopt(
    std::shared_ptr<XER::Audio::Core::AudioBuffer> buf) {
    if (!buf || NativeBufferTemplateStore().IsEmpty()) return {};
    auto* wrap = new NativeBufferWrap(0, 1, 32, 0);
    wrap->a_ = std::move(buf);
    v8::Local<v8::Object> obj =
        v8::Local<v8::FunctionTemplate>::New(
            v8::Isolate::GetCurrent(), NativeBufferTemplateStore())
            ->InstanceTemplate()
            ->NewInstance(Nan::GetCurrentContext())
            .ToLocalChecked();
    wrap->Wrap(obj);
    return obj;
}

NativeBufferWrap* UnwrapNativeBuffer(v8::Local<v8::Value> v) {
    return UnwrapT<NativeBufferWrap>(v);
}

void NativeBufferWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new NativeBufferWrap(IntArg(info, 0, 44100),
        IntArg(info, 1, 2), IntArg(info, 2, 32), IntArg(info, 3, 0));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void NativeBufferWrap::Resize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Buf().Resize(
        static_cast<size_t>(IntArg(info, 0, 0)))));
}

void NativeBufferWrap::GetFrameCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Buf().GetFrameCount())));
}

void NativeBufferWrap::GetSampleCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Buf().GetSampleCount())));
}

void NativeBufferWrap::GetByteSize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Buf().GetByteSize())));
}

void NativeBufferWrap::Clear(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    if (!w) return;
    w->Buf().Clear();
}

void NativeBufferWrap::Fill(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    if (!w) return;
    w->Buf().Fill(static_cast<float>(NumArg(info, 0, 0.0)));
}

void NativeBufferWrap::GetFloatData(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapNativeBuffer(info.This());
    if (!w) return;
    const size_t n = w->Buf().GetSampleCount();
    info.GetReturnValue().Set(F32ToJS(w->Buf().GetFloatData(), n));
}


void InitCoreBufferManager(v8::Local<v8::Object> ns) {
    v8::Local<v8::FunctionTemplate> tpl =
        Nan::New<v8::FunctionTemplate>(NativeBufferWrap::New);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New("NativeBuffer").ToLocalChecked());
    Nan::SetMethod(tpl->PrototypeTemplate(), "Resize", NativeBufferWrap::Resize);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetFrameCount", NativeBufferWrap::GetFrameCount);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetSampleCount", NativeBufferWrap::GetSampleCount);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetByteSize", NativeBufferWrap::GetByteSize);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Clear", NativeBufferWrap::Clear);
    Nan::SetMethod(tpl->PrototypeTemplate(), "Fill", NativeBufferWrap::Fill);
    Nan::SetMethod(tpl->PrototypeTemplate(), "GetFloatData", NativeBufferWrap::GetFloatData);
    NativeBufferTemplateStore().Reset(v8::Isolate::GetCurrent(), tpl);
    Nan::Set(ns, Nan::New("NativeBuffer").ToLocalChecked(),
             Nan::GetFunction(tpl).ToLocalChecked());
}

} // namespace xer::addon::audio::core
