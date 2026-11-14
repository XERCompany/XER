// xer.audio.fx.Bitcrush — DistortionBitcrush.

#include "bitcrush.h"

#include <vector>

namespace xer::addon::audio::fx {

using XER::Audio::FX::DistortionBitcrush;

BitcrushWrap::BitcrushWrap() = default;
BitcrushWrap::~BitcrushWrap() = default;

BitcrushWrap* UnwrapBitcrush(v8::Local<v8::Value> v) {
    return UnwrapT<BitcrushWrap>(v);
}

void BitcrushWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new BitcrushWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void BitcrushWrap::SetBitDepth(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitcrush(info.This());
    if (!w) return;
    w->Get().SetBitDepth(int(IntArg(info, 0, 8)));
}

void BitcrushWrap::SetDownsampleFactor(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitcrush(info.This());
    if (!w) return;
    w->Get().SetDownsampleFactor(int(IntArg(info, 0, 1)));
}

void BitcrushWrap::ProcessBlock(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitcrush(info.This());
    if (!w || info.Length() < 1) return;
    std::vector<float> in;
    if (!F32FromJS(info[0], &in)) {
        Nan::ThrowError("Bitcrush.ProcessBlock expects Float32Array");
        return;
    }
    std::vector<float> out(in.size(), 0.0f);
    if (w->Get().ProcessBlock(in.data(), in.size(), out.data())) {
        info.GetReturnValue().Set(F32ToJS(out));
    }
}

void BitcrushWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapBitcrush(info.This());
    info.GetReturnValue().Set(Nan::New(
        w ? w->Get().GetReport() : std::string()).ToLocalChecked());
}

void InitFxBitcrush(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Bitcrush", BitcrushWrap::New,
{         {"SetBitDepth", BitcrushWrap::SetBitDepth},
         {"SetDownsampleFactor", BitcrushWrap::SetDownsampleFactor},
         {"ProcessBlock", BitcrushWrap::ProcessBlock},
         {"Process", BitcrushWrap::ProcessBlock},
         {"GetReport", BitcrushWrap::GetReport}});
}

} // namespace xer::addon::audio::fx
