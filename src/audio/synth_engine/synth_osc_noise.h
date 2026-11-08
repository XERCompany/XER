#pragma once

namespace XER::Audio::Synth {

class SynthOscNoise {
public:
    void SetSeed(unsigned int seed);
    float Process();

private:
    unsigned int state_ = 0x12345678u;
};

}  // namespace XER::Audio::Synth
