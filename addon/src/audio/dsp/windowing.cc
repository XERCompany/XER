// xer.audio.dsp.Windowing — window function generators + ApplyWindow.

#include "windowing.h"

#include <vector>

#include "audio/dsp_algorithms/dsp_windowing_functions.h"

namespace xer::addon::audio::dsp {

using XER::Audio::DSP::WindowingFunctions;

namespace {

using GenFn = std::vector<float> (*)(size_t);

NAN_METHOD(WindowGen) {
    // info.Data() == generator pointer (Hann/SqrtHann/Hamming/Blackman).
    const auto gen =
        *static_cast<GenFn*>(info.Data().As<v8::External>()->Value());
    const size_t n = static_cast<size_t>(IntArg(info, 0, 1024));
    info.GetReturnValue().Set(F32ToJS(gen(n > 0 ? n : 1)));
}

NAN_METHOD(WindowApply) {
    std::vector<float> window, samples;
    if (info.Length() < 2 || !F32FromJS(info[0], &window) ||
        !F32FromJS(info[1], &samples)) {
        Nan::ThrowError("Windowing.Apply expects (window, samples)");
        return;
    }
    WindowingFunctions::ApplyWindow(window, samples.data(), samples.size());
    info.GetReturnValue().Set(F32ToJS(samples));
}

NAN_METHOD(WindowDescribe) {
    std::vector<float> window;
    if (info.Length() < 1 || !F32FromJS(info[0], &window)) {
        Nan::ThrowError("Windowing.Describe expects Float32Array");
        return;
    }
    info.GetReturnValue().Set(
        Nan::New(WindowingFunctions::DescribeWindow(window))
            .ToLocalChecked());
}

} // namespace

void InitDspWindowing(v8::Local<v8::Object> ns) {
    Nan::Set(ns, Nan::New("Hann").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 WindowGen, Nan::New<v8::External>(
                     new GenFn(&WindowingFunctions::GenerateHann))))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("SqrtHann").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 WindowGen, Nan::New<v8::External>(
                     new GenFn(&WindowingFunctions::GenerateSqrtHann))))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("Hamming").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 WindowGen, Nan::New<v8::External>(
                     new GenFn(&WindowingFunctions::GenerateHamming))))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("Blackman").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(
                 WindowGen, Nan::New<v8::External>(
                     new GenFn(&WindowingFunctions::GenerateBlackman))))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("Apply").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(WindowApply))
                 .ToLocalChecked());
    Nan::Set(ns, Nan::New("Describe").ToLocalChecked(),
             Nan::GetFunction(Nan::New<v8::FunctionTemplate>(WindowDescribe))
                 .ToLocalChecked());
}

} // namespace xer::addon::audio::dsp
