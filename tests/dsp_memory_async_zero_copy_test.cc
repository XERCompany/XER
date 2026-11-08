#include "audio/dsp_algorithms/dsp_convolution_engine.h"
#include "audio/dsp_algorithms/dsp_fir_filter_bank.h"
#include "audio/effects_rack/fx_eq_parametric.h"
#include "audio/effects_rack/fx_mod_phaser.h"
#include "audio/effects_rack/fx_mod_flanger.h"
#include "audio/effects_rack/fx_mod_chorus.h"
#include "audio/effects_rack/fx_reverb_algorithmic.h"
#include <cassert>
#include <future>
#include <iostream>
#include <vector>

using namespace XER::Audio;

void TestMemoryStats() {
    DSP::ConvolutionEngine conv;
    std::cout << "ConvolutionEngine memory:\n" << conv.GetMemoryStats();
    DSP::FIRFilterBank fir;
    std::cout << "FIRFilterBank memory:\n" << fir.GetMemoryStats();
    FX::ParametricEQ eq;
    std::cout << "ParametricEQ memory:\n" << eq.GetMemoryStats();
    FX::ModPhaser phaser;
    std::cout << "ModPhaser memory:\n" << phaser.GetMemoryStats();
    FX::ModFlanger flanger;
    std::cout << "ModFlanger memory:\n" << flanger.GetMemoryStats();
    FX::ModChorus chorus;
    std::cout << "ModChorus memory:\n" << chorus.GetMemoryStats();
    FX::ReverbAlgorithmic reverb;
    std::cout << "ReverbAlgorithmic memory:\n" << reverb.GetMemoryStats();
}

void TestAsyncAndZeroCopy() {
    DSP::ConvolutionEngine conv;
    std::vector<float> ir = {1.0f, 0.5f, 0.25f};
    conv.SetImpulseResponse(ir.data(), ir.size());
    std::vector<float> input(16, 1.0f);
    std::vector<float> output(16, 0.0f);
    // Zero-copy test
    assert(conv.ProcessBlock(input.data(), input.size(), input.data()));
    // Async test
    std::vector<float> out2(16, 0.0f);
    auto fut = conv.ProcessBlockAsync(input.data(), input.size(), out2.data());
    assert(fut.get());
    // FIRFilterBank
    DSP::FIRFilterBank fir;
    fir.AddFilter({1.0f, -1.0f});
    std::vector<float> fir_out(1);
    assert(fir.ProcessSample(1.0f, fir_out));
    auto fir_fut = fir.ProcessSampleAsync(1.0f, fir_out);
    assert(fir_fut.get());
}

int main() {
    TestMemoryStats();
    TestAsyncAndZeroCopy();
    std::cout << "[OK] All memory/async/zero-copy tests passed.\n";
    return 0;
}
