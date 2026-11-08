#pragma once

#include <vector>
 
#include <fftw3.h> 

namespace XER::Audio::Synth {

class SynthOscWavetable {
public:
    void SetTable(const std::vector<float>& table);
    void BuildBandlimitedTable();
    void SetFrequency(float hz);
    float Process(float sample_rate);

private:
    std::vector<float> table_;
    float frequency_hz_ = 440.0f;
    float phase_ = 0.0f;
};

}  // namespace XER::Audio::Synth
