#pragma once

// xer.audio.analysis.Spectrogram — wraps XER::Audio::AnalysisAI::SpectrogramGenerator.

#include <memory>

#include "audio/analysis_ai/anal_spectrogram_generator.h"

#include "audio/audio_util.h"

namespace xer::addon::audio::analysis {

class SpectrogramWrap : public Nan::ObjectWrap {
public:
    explicit SpectrogramWrap(size_t fft_size, size_t hop_size);
    ~SpectrogramWrap() override;

    XER::Audio::AnalysisAI::SpectrogramGenerator& Get() { return a_; }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Generate(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void ExportAsRaw(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void Reset(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetMagnitudeMatrix(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetFrameCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetBinCount(const Nan::FunctionCallbackInfo<v8::Value>& info);
    static void GetReport(const Nan::FunctionCallbackInfo<v8::Value>& info);

private:
    XER::Audio::AnalysisAI::SpectrogramGenerator a_;
};

SpectrogramWrap* UnwrapSpectrogramGenerator(v8::Local<v8::Value> v);
void InitAnalysisSpectrogram(v8::Local<v8::Object> ns);

} // namespace xer::addon::audio::analysis
