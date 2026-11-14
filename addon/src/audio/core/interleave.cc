// xer.audio.core.Interleave — AudioInterleaveProcessor binding.

#include "interleave.h"

#include <vector>
#include <string>

namespace xer::addon::audio::core {

using XER::Audio::Core::AudioInterleaveProcessor;

InterleaveWrap::InterleaveWrap() = default;
InterleaveWrap::~InterleaveWrap() = default;

InterleaveWrap* UnwrapInterleave(v8::Local<v8::Value> v) {
    return UnwrapT<InterleaveWrap>(v);
}

void InterleaveWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new InterleaveWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void InterleaveWrap::Interleave(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapInterleave(info.This());
    if (!w) return;
    if (info.Length() < 1 || !info[0]->IsArray()) return;
    auto chans = info[0].As<v8::Array>();
    const int nc = static_cast<int>(chans->Length());
    if (nc == 0) return;
    std::vector<std::vector<float>> data(nc);
    size_t frames = 0;
    for (int c = 0; c < nc; ++c) {
        v8::Local<v8::Value> cv;
        if (!chans->Get(Nan::GetCurrentContext(), c).ToLocal(&cv)) return;
        if (!F32FromJS(cv, &data[c])) return;
        frames = std::max(frames, data[c].size());
    }
    std::vector<const float*> ptrs(nc);
    for (int c = 0; c < nc; ++c) {
        data[c].resize(frames, 0.0f);
        ptrs[c] = data[c].data();
    }
    std::vector<float> out(frames * nc, 0.0f);
    if (!XER::Audio::Core::AudioInterleaveProcessor::Interleave(
            ptrs.data(), nc, frames, out.data())) return;
    info.GetReturnValue().Set(F32ToJS(out));
}

void InterleaveWrap::Deinterleave(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapInterleave(info.This());
    if (!w) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Interleave.Deinterleave expects Float32Array");
        return;
    }
    const int nc = IntArg(info, 1, 2);
    const size_t frames = nc > 0 ? in.size() / nc : 0;
    std::vector<std::vector<float>> chans(nc, std::vector<float>(frames));
    std::vector<float*> ptrs(nc);
    for (int c = 0; c < nc; ++c) ptrs[c] = chans[c].data();
    if (!XER::Audio::Core::AudioInterleaveProcessor::Deinterleave(
            in.data(), nc, frames, ptrs.data())) return;
    auto arr = Nan::New<v8::Array>(nc);
    for (int c = 0; c < nc; ++c)
        Nan::Set(arr, c, F32ToJS(chans[c]));
    info.GetReturnValue().Set(arr);
}


void InitCoreInterleave(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Interleave", InterleaveWrap::New,
        {{"Interleave", InterleaveWrap::Interleave},
         {"Deinterleave", InterleaveWrap::Deinterleave}});
}

} // namespace xer::addon::audio::core
