#include "fx_reverb_algorithmic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <future>

namespace XER::Engine::Audio::FX {

std::string ReverbAlgorithmic::GetMemoryStats() const {
	std::string stats;
	for (size_t index = 0; index < combs_.size(); ++index) {
		stats += "Comb" + std::to_string(index) + ": " + std::to_string(combs_[index].GetReport().size()) + " bytes (report string)\n";
	}
	for (size_t index = 0; index < allpasses_.size(); ++index) {
		stats += "Allpass" + std::to_string(index) + ": " + std::to_string(allpasses_[index].GetReport().size()) + " bytes (report string)\n";
	}
	return stats;
}

ReverbAlgorithmic::ReverbAlgorithmic()
	: sample_rate_(44100.0f),
	  room_size_(0.6f),
	  damping_(0.3f),
	  mix_(0.25f) {
	perf_counter_.Enable();
}

ReverbAlgorithmic::~ReverbAlgorithmic() = default;

bool ReverbAlgorithmic::Initialize(float sample_rate, size_t max_delay_samples) {
	if (sample_rate <= 0.0f || max_delay_samples < 32) {
		return false;
	}
	sample_rate_ = sample_rate;
	bool ok = true;
	for (auto& comb : combs_) {
		ok = comb.Initialize(max_delay_samples) && ok;
	}
	for (auto& allpass : allpasses_) {
		ok = allpass.Initialize(max_delay_samples) && ok;
	}
	for (auto& early : early_reflections_) {
		ok = early.Initialize(max_delay_samples) && ok;
	}
	comb_damping_state_.fill(0.0f);
	allpass_state_.fill(0.0f);
	return ok;
}

void ReverbAlgorithmic::SetRoomSize(float room_size) {
	room_size_ = std::clamp(room_size, 0.1f, 1.0f);
}

void ReverbAlgorithmic::SetDamping(float damping) {
	damping_ = std::clamp(damping, 0.0f, 0.99f);
}

void ReverbAlgorithmic::SetMix(float mix) {
	mix_ = std::clamp(mix, 0.0f, 1.0f);
}

bool ReverbAlgorithmic::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr) {
		return false;
	}
	perf_counter_.StartCounter("fx_reverb_algo");
	static constexpr std::array<float, 6> kCombBaseMs = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 61.7f};
	static constexpr std::array<float, 3> kAllpassBaseMs = {5.0f, 7.7f, 12.3f};
	static constexpr std::array<float, 4> kEarlyBaseMs = {8.0f, 13.0f, 21.0f, 34.0f};
	static constexpr std::array<float, 4> kEarlyGain = {0.46f, 0.32f, 0.24f, 0.18f};
	const float room_scale = 0.72f + room_size_ * 1.55f;
	const float feedback = std::clamp(0.62f + room_size_ * 0.31f, 0.62f, 0.93f);
	const float damping_blend = std::clamp(damping_, 0.0f, 0.97f);

	for (size_t index = 0; index < combs_.size(); ++index) {
		const auto samples = static_cast<size_t>((kCombBaseMs[index] * room_scale * sample_rate_) / 1000.0f);
		combs_[index].SetDelaySamples(std::max<size_t>(8u, samples));
	}
	for (size_t index = 0; index < allpasses_.size(); ++index) {
		const auto samples = static_cast<size_t>((kAllpassBaseMs[index] * (0.85f + room_size_ * 0.7f) * sample_rate_) / 1000.0f);
		allpasses_[index].SetDelaySamples(std::max<size_t>(4u, samples));
	}
	for (size_t index = 0; index < early_reflections_.size(); ++index) {
		const auto samples = static_cast<size_t>((kEarlyBaseMs[index] * (0.7f + room_size_ * 0.8f) * sample_rate_) / 1000.0f);
		early_reflections_[index].SetDelaySamples(std::max<size_t>(2u, samples));
	}

	for (size_t n = 0; n < frame_count; ++n) {
		const float dry = input[n];
		float early = 0.0f;
		for (size_t index = 0; index < early_reflections_.size(); ++index) {
			early += early_reflections_[index].Process(dry) * kEarlyGain[index];
		}

		float tail = 0.0f;
		for (size_t index = 0; index < combs_.size(); ++index) {
			const float delayed = combs_[index].Process(dry + comb_damping_state_[index] * feedback);
			comb_damping_state_[index] = delayed * (1.0f - damping_blend) + comb_damping_state_[index] * damping_blend;
			tail += comb_damping_state_[index];
		}
		tail *= 1.0f / static_cast<float>(combs_.size());

		float diffused = tail + early * 0.42f;
		for (size_t index = 0; index < allpasses_.size(); ++index) {
			const float delayed = allpasses_[index].Process(diffused + allpass_state_[index] * 0.58f);
			allpass_state_[index] = delayed;
			diffused = delayed - diffused * 0.58f;
		}

		const float wet = std::tanh((early * 0.35f + diffused) * 1.18f);
		output[n] = dry * (1.0f - mix_) + wet * mix_;
	}
	perf_counter_.StopCounter("fx_reverb_algo");
	return true;
}

std::string ReverbAlgorithmic::GetReport() const {
	return "ReverbAlgo: room=" + std::to_string(room_size_) +
		", damp=" + std::to_string(damping_) +
		", mix=" + std::to_string(mix_) +
		", combs=6, allpasses=3, early_reflections=4";
}


std::future<bool> ReverbAlgorithmic::ProcessBlockAsync(const float* input, size_t frame_count, float* output) {
	return std::async(std::launch::async, [this, input, frame_count, output]() {
		return this->ProcessBlock(input, frame_count, output);
	});
}

}  // namespace XER::Engine::Audio::FX
