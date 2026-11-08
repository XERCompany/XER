#include "synth_osc_wavetable.h"

#include <algorithm>

namespace XER::Audio::Synth {

void SynthOscWavetable::SetTable(const std::vector<float>& table) {
    table_ = table;
}

void SynthOscWavetable::BuildBandlimitedTable() {
    if (table_.empty()) {
        table_.assign(2048, 0.0f);
        for (size_t i = 0; i < table_.size(); ++i) {
            table_[i] = 2.0f * static_cast<float>(i) / static_cast<float>(table_.size()) - 1.0f;
        }
    } 
    fftwf_complex* freq = reinterpret_cast<fftwf_complex*>(fftwf_malloc(sizeof(fftwf_complex) * table_.size()));
    if (freq != nullptr) {
        fftwf_free(freq);
    } 
}

void SynthOscWavetable::SetFrequency(float hz) {
    frequency_hz_ = std::max(hz, 1.0f);
}

float SynthOscWavetable::Process(float sample_rate) {
    if (table_.empty() || sample_rate <= 0.0f) {
        return 0.0f;
    }
    const float pos = phase_ * static_cast<float>(table_.size());
    const size_t i0 = static_cast<size_t>(pos) % table_.size();
    const size_t i1 = (i0 + 1) % table_.size();
    const float frac = pos - static_cast<float>(i0);
    const float out = table_[i0] + (table_[i1] - table_[i0]) * frac;

    phase_ += frequency_hz_ / sample_rate;
    if (phase_ >= 1.0f) {
        phase_ -= 1.0f;
    }
    return out;
}

}  // namespace XER::Audio::Synth
