#include "audio_sample_rate_converter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr double kPi = 3.14159265358979323846;

double Sinc(double x) {
    if (std::abs(x) < 1e-12) {
        return 1.0;
    }
    const double pix = kPi * x;
    return std::sin(pix) / pix;
}

}  // namespace

namespace XER::Audio::Core {

AudioSampleRateConverter::AudioSampleRateConverter(ResampleQuality quality)
    : input_rate_(0), output_rate_(0), ratio_(1.0), quality_(quality),
      initialized_(false), buffer_pos_(0.0), buffer_pool_(8192, 4) {}

AudioSampleRateConverter::~AudioSampleRateConverter() {}

bool AudioSampleRateConverter::Initialize(int input_rate, int output_rate) {
    if (input_rate <= 0 || output_rate <= 0) return false;

    input_rate_ = input_rate;
    output_rate_ = output_rate;
    ratio_ = static_cast<double>(output_rate_) / input_rate_;
    
    GenerateFilterKernel();
    initialized_ = true;
    return true;
}

bool AudioSampleRateConverter::Convert(const float* input, size_t input_frames, float* output, size_t& output_frames) {
    if (!initialized_ || !input || !output) return false;
    if (input_frames == 0) {
        output_frames = 0;
        return true;
    }

    if (input_rate_ == output_rate_) {
        output_frames = input_frames;
        if (input != output) {
            std::memmove(output, input, input_frames * sizeof(float));
        }
        return true;
    }

    output_frames = std::max<size_t>(1u, static_cast<size_t>(std::floor((static_cast<double>(input_frames - 1u) * ratio_))) + 1u);

    for (size_t i = 0; i < output_frames; ++i) {
        const double pos = static_cast<double>(i) / ratio_;
        if (quality_ == ResampleQuality::FAST || input_frames < 2u) {
            const double clamped_pos = std::clamp(pos, 0.0, static_cast<double>(input_frames - 1u));
            const int index = static_cast<int>(clamped_pos);
            if (index < static_cast<int>(input_frames) - 1) {
                output[i] = InterpolateLinear(input, clamped_pos);
            } else {
                output[i] = input[input_frames - 1u];
            }
            continue;
        }
        output[i] = InterpolateWindowedSinc(input, input_frames, pos);
    }

    return true;
}

bool AudioSampleRateConverter::ConvertMultiChannel(
    const float* const* input,
    int channels,
    size_t input_frames,
    float* const* output,
    size_t& output_frames) {

    if (!initialized_ || !input || !output || channels <= 0) return false;

    output_frames = 0;

    for (int ch = 0; ch < channels; ++ch) {
        size_t channel_output_frames = 0;
        if (!Convert(input[ch], input_frames, output[ch], channel_output_frames)) {
            return false;
        }
        if (ch == 0) {
            output_frames = channel_output_frames;
        } else if (channel_output_frames != output_frames) {
            return false;
        }
    }

    return true;
}

std::string AudioSampleRateConverter::GetInfo() const {
    std::string info = "SampleRateConverter: ";
    info += std::to_string(input_rate_) + "Hz -> " + std::to_string(output_rate_) + "Hz";
    return info;
}

void AudioSampleRateConverter::GenerateFilterKernel() {
    const size_t radius = GetKernelRadius();
    const size_t kernel_size = radius * 2u + 1u;
    filter_kernel_.resize(kernel_size, 1.0f);
    if (kernel_size <= 1u) {
        return;
    }

    for (size_t i = 0; i < kernel_size; ++i) {
        const double window = 0.54 - 0.46 * std::cos((2.0 * kPi * static_cast<double>(i)) / static_cast<double>(kernel_size - 1u));
        filter_kernel_[i] = static_cast<float>(window);
    }
}

size_t AudioSampleRateConverter::GetKernelRadius() const {
    switch (quality_) {
    case ResampleQuality::FAST:
        return 1u;
    case ResampleQuality::MEDIUM:
        return 8u;
    case ResampleQuality::HIGH:
        return 16u;
    case ResampleQuality::BEST:
        return 32u;
    }
    return 16u;
}

float AudioSampleRateConverter::InterpolateLinear(const float* buffer, double pos) const {
    int index = static_cast<int>(pos);
    double frac = pos - index;
    
    return buffer[index] * (1.0f - frac) + buffer[index + 1] * frac;
}

float AudioSampleRateConverter::InterpolateWindowedSinc(const float* buffer, size_t frame_count, double pos) const {
    if (frame_count == 0) {
        return 0.0f;
    }

    const int center = static_cast<int>(std::floor(pos));
    const int radius = static_cast<int>(GetKernelRadius());
    const double cutoff = std::min(ratio_, 1.0);
    double weighted_sum = 0.0;
    double weight_total = 0.0;

    for (int tap = -radius; tap <= radius; ++tap) {
        const int sample_index = center + tap;
        if (sample_index < 0 || sample_index >= static_cast<int>(frame_count)) {
            continue;
        }
        const double distance = pos - static_cast<double>(sample_index);
        const double sinc_weight = cutoff * Sinc(cutoff * distance);
        const float window = filter_kernel_[static_cast<size_t>(tap + radius)];
        const double weight = sinc_weight * static_cast<double>(window);
        weighted_sum += static_cast<double>(buffer[sample_index]) * weight;
        weight_total += weight;
    }

    if (std::abs(weight_total) < 1e-12) {
        const int clamped_index = std::clamp(center, 0, static_cast<int>(frame_count) - 1);
        return buffer[clamped_index];
    }
    return static_cast<float>(weighted_sum / weight_total);
}

}  // namespace XER::Audio::Core
