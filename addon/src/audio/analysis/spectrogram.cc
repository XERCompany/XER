// xer.audio.analysis.Spectrogram — SpectrogramGenerator binding.

#include "spectrogram.h"

#include <vector>
#include <string>

namespace xer::addon::audio::analysis {

using XER::Audio::AnalysisAI::SpectrogramGenerator;

SpectrogramWrap::SpectrogramWrap(size_t fft_size, size_t hop_size) : a_(fft_size, hop_size) {}
SpectrogramWrap::~SpectrogramWrap() = default;

SpectrogramWrap* UnwrapSpectrogramGenerator(v8::Local<v8::Value> v) {
    return UnwrapT<SpectrogramWrap>(v);
}

void SpectrogramWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new SpectrogramWrap(IntArg(info, 0, 1024), IntArg(info, 1, 256));
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

void SpectrogramWrap::Generate(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    std::vector<float> in;
        if (!F32FromJS(info[0], &in)) {
            Nan::ThrowError("Spectrogram.Generate expects Float32Array");
            return;
        }
        info.GetReturnValue().Set(Nan::New(w->Get().Generate(
            in.data(), in.size())));
}

void SpectrogramWrap::ExportAsRaw(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().ExportAsRaw(
            StrArg(info, 0))));
}

void SpectrogramWrap::Reset(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    w->Get().Reset();
}

void SpectrogramWrap::GetMagnitudeMatrix(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    const auto& m = w->Get().GetMagnitudeMatrix();
        auto outer = Nan::New<v8::Array>(m.size());
        for (size_t r = 0; r < m.size(); ++r) {
            Nan::Set(outer, static_cast<uint32_t>(r), F32ToJS(m[r]));
        }
        info.GetReturnValue().Set(outer);
}

void SpectrogramWrap::GetFrameCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetFrameCount())));
}

void SpectrogramWrap::GetBinCount(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(static_cast<double>(w->Get().GetBinCount())));
}

void SpectrogramWrap::GetReport(
    const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = UnwrapSpectrogramGenerator(info.This());
    if (!w) return;
    info.GetReturnValue().Set(Nan::New(w->Get().GetReport()).ToLocalChecked());
}

void InitAnalysisSpectrogram(v8::Local<v8::Object> ns) {
    RegisterAudioClass(ns, "Spectrogram", SpectrogramWrap::New,
        {{"Generate", SpectrogramWrap::Generate},
         {"ExportAsRaw", SpectrogramWrap::ExportAsRaw},
         {"Reset", SpectrogramWrap::Reset},
         {"GetMagnitudeMatrix", SpectrogramWrap::GetMagnitudeMatrix},
         {"GetFrameCount", SpectrogramWrap::GetFrameCount},
         {"GetBinCount", SpectrogramWrap::GetBinCount},
         {"GetReport", SpectrogramWrap::GetReport}});
}

} // namespace xer::addon::audio::analysis
