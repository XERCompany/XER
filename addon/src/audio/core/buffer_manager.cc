// xer.audio.core.BufferManager — AudioBufferManager binding.

#include "buffer_manager.h"

#include <string>
#include <vector>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioBufferManager;
using XER::Audio::Core::AudioBuffer;
using XER::Audio::Core::AudioFrameInfo;
using XER::Audio::Core::AudioLayout;

BufferManagerWrap::BufferManagerWrap(size_t pool_size) : a_(pool_size) {}
BufferManagerWrap::~BufferManagerWrap() = default;

BufferManagerWrap* UnwrapBufferManager(v8::Local<v8::Value> v) {
    return UnwrapT<BufferManagerWrap>(v);
}

void BufferManagerWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new BufferManagerWrap(
        static_cast<size_t>(IntArg(info, 0, 4)));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

namespace {

bool ReadFrameInfo(v8::Local<v8::Value> v, AudioFrameInfo* info) {
    if (v.IsEmpty() || !v->IsObject()) return false;
    v8::Local<v8::Object> o = v.As<v8::Object>();
    auto prop = [&](const char* k, v8::Local<v8::Value>* dst) {
        return Nan::Get(o, Nan::New(k).ToLocalChecked()).ToLocal(dst);
    };
    v8::Local<v8::Value> x;
    if (prop("sampleRate", &x) && x->IsNumber())
        info->sample_rate = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("channelCount", &x) && x->IsNumber())
        info->channel_count = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("channels", &x) && x->IsNumber())
        info->channel_count = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("bitDepth", &x) && x->IsNumber())
        info->bit_depth = static_cast<int>(x.As<v8::Number>()->Value());
    if (prop("layout", &x) && x->IsNumber())
        info->layout = static_cast<AudioLayout>(
            static_cast<int>(x.As<v8::Number>()->Value()));
    return info->sample_rate > 0 && info->channel_count > 0;
}

} // namespace

void BufferManagerWrap::AcquireBuffer(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    if (!w || info.Length() < 1) return;
    AudioFrameInfo fi{};
    if (!ReadFrameInfo(info[0], &fi)) {
        Nan::ThrowError(
            "BufferManager.AcquireBuffer expects {sampleRate, channelCount,"
            " bitDepth?, layout?}");
        return;
    }
    const size_t frames = static_cast<size_t>(IntArg(info, 1, 0));
    auto buf = w->Get().AcquireBuffer(fi, frames);
    if (!buf) {
        Nan::ThrowError("BufferManager.AcquireBuffer failed");
        return;
    }
    info.GetReturnValue().Set(NativeBufferWrap::Adopt(std::move(buf)));
}

void BufferManagerWrap::ReleaseBuffer(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    if (!w || info.Length() < 1) return;
    auto* nb = UnwrapNativeBuffer(info[0]);
    if (!nb) {
        Nan::ThrowError("BufferManager.ReleaseBuffer expects NativeBuffer");
        return;
    }
    w->Get().ReleaseBuffer(nb->Shared());
}

void BufferManagerWrap::Resize(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    if (!w) return;
    w->Get().Resize(static_cast<size_t>(IntArg(info, 0, 0)));
}

void BufferManagerWrap::Clear(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    if (!w) return;
    w->Get().Clear();
}

void BufferManagerWrap::GetAvailableBuffers(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(w ? w->Get().GetAvailableBuffers() : 0)));
}

void BufferManagerWrap::GetTotalBuffers(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    info.GetReturnValue().Set(
        Nan::New(static_cast<double>(w ? w->Get().GetTotalBuffers() : 0)));
}

void BufferManagerWrap::GetPoolStatus(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBufferManager(info.This());
    info.GetReturnValue().Set(
        Nan::New(w ? w->Get().GetPoolStatus() : std::string()).ToLocalChecked());
}


void InitCoreBufferPool(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "BufferManager", BufferManagerWrap::New,
        {{"AcquireBuffer", BufferManagerWrap::AcquireBuffer},
         {"ReleaseBuffer", BufferManagerWrap::ReleaseBuffer},
         {"Resize", BufferManagerWrap::Resize},
         {"Clear", BufferManagerWrap::Clear},
         {"GetAvailableBuffers", BufferManagerWrap::GetAvailableBuffers},
         {"GetTotalBuffers", BufferManagerWrap::GetTotalBuffers},
         {"GetPoolStatus", BufferManagerWrap::GetPoolStatus}});
}

} // namespace xer::addon::audio::core
