#include "helper/string_utils.h"
#include "custom_effect_advanced_processing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>

#include "custom_effect_accel.h"
#include "custom_effect_core.h"

namespace XER::Engine::Audio::FX {
namespace {

constexpr float kPi = 3.14159265358979323846f;

std::string NormalizeName(const std::string& text) {
	std::string lowered = XER::Engine::Helper::String::ToLowerAscii(text);
	std::string result;
	result.reserve(lowered.size());
	for (unsigned char ch : lowered) {
		if (std::isalnum(ch)) {
			result.push_back(static_cast<char>(ch));
		}
	}
	return result;
}

float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}

bool IsAdvancedEffectName(const std::string& normalized_name) {
	return normalized_name == "pulteclowend"
		|| normalized_name == "sslbuscomp"
		|| normalized_name == "transientpunch"
		|| normalized_name == "exciterair"
		|| normalized_name == "la2avocal"
		|| normalized_name == "radioannouncer"
		|| normalized_name == "silkydeesser"
		|| normalized_name == "tapemachine"
		|| normalized_name == "vinylmaster"
		|| normalized_name == "springtank"
		|| normalized_name == "dimensionchorus"
		|| normalized_name == "spaceecho"
		|| normalized_name == "shimmerbloom"
		|| normalized_name == "psychowidener";
}

void SoftClipInPlace(std::vector<float>* samples, float drive, float makeup) {
	if (samples == nullptr || samples->empty()) {
		return;
	}
	for (float& sample : *samples) {
		const float driven = sample * drive;
		sample = (driven / (1.0f + std::abs(driven))) * makeup;
	}
}

void ApplyTiltToneInPlace(std::vector<float>* samples, float low_gain, float high_gain, float center_hz, float sample_rate) {
	if (samples == nullptr || samples->empty() || sample_rate <= 0.0f) {
		return;
	}
	const float alpha = std::clamp(2.0f * kPi * center_hz / sample_rate, 0.0005f, 0.45f);
	float low = 0.0f;
	for (float& sample : *samples) {
		low += alpha * (sample - low);
		const float high = sample - low;
		sample = low * low_gain + high * high_gain;
	}
}

void ApplyCompressorInPlace(std::vector<float>* samples, float threshold_db, float ratio, float attack, float release, float makeup_db) {
	if (samples == nullptr || samples->empty()) {
		return;
	}
	const float threshold = DbToLinear(threshold_db);
	const float makeup = DbToLinear(makeup_db);
	float envelope = 0.0f;
	float gain = 1.0f;
	for (float& sample : *samples) {
		const float level = std::abs(sample);
		envelope += ((level > envelope) ? attack : release) * (level - envelope);
		float target_gain = 1.0f;
		if (envelope > threshold && envelope > 0.0f) {
			const float over = envelope / threshold;
			target_gain = std::pow(over, (1.0f / ratio) - 1.0f);
		}
		gain += 0.08f * (target_gain - gain);
		sample *= gain * makeup;
	}
}

void ApplyTransientFocusInPlace(std::vector<float>* samples, float amount) {
	if (samples == nullptr || samples->size() < 2u) {
		return;
	}
	float envelope = 0.0f;
	float previous_envelope = 0.0f;
	for (float& sample : *samples) {
		previous_envelope = envelope;
		envelope += 0.035f * (std::abs(sample) - envelope);
		const float attack = std::max(0.0f, envelope - previous_envelope);
		sample *= 1.0f + std::clamp(attack * amount, 0.0f, 0.34f);
	}
}

void AddHighExciter(std::vector<float>* samples, float amount, float sample_rate) {
	if (samples == nullptr || samples->empty()) {
		return;
	}
	std::vector<float> high = *samples;
	ApplyTiltToneInPlace(&high, 0.0f, 1.0f, std::clamp(sample_rate * 0.11f, 4200.0f, 9200.0f), sample_rate);
	SoftClipInPlace(&high, 3.4f, amount);
	AddScaledBuffer(high.data(), high.size(), 1.0f, samples->data());
}

void ApplyDelayBlend(const std::vector<float>& source, size_t delay_samples, float feedback, float mix, std::vector<float>* samples) {
	if (samples == nullptr || samples->empty() || delay_samples == 0u) {
		return;
	}
	std::vector<float> delay_line(delay_samples + 1u, 0.0f);
	size_t cursor = 0u;
	for (size_t index = 0; index < samples->size(); ++index) {
		const float delayed = delay_line[cursor];
		delay_line[cursor] = source[index] + delayed * feedback;
		(*samples)[index] = (*samples)[index] * (1.0f - mix) + delayed * mix;
		cursor = (cursor + 1u) % delay_line.size();
	}
}

void ApplyModulatedDelayBlend(const std::vector<float>& source, float base_delay, float depth, float rate_hz, float sample_rate, float mix, std::vector<float>* samples) {
	if (samples == nullptr || samples->empty() || sample_rate <= 0.0f) {
		return;
	}
	const size_t max_delay = static_cast<size_t>(std::max(8.0f, base_delay + depth + 8.0f));
	std::vector<float> delay_line(max_delay + 1u, 0.0f);
	size_t cursor = 0u;
	for (size_t index = 0; index < samples->size(); ++index) {
		const float phase = 2.0f * kPi * rate_hz * static_cast<float>(index) / sample_rate;
		const size_t delay = static_cast<size_t>(std::clamp(base_delay + std::sin(phase) * depth, 1.0f, static_cast<float>(max_delay)));
		const size_t read = (cursor + delay_line.size() - delay) % delay_line.size();
		const float delayed = delay_line[read];
		delay_line[cursor] = source[index];
		(*samples)[index] = (*samples)[index] * (1.0f - mix) + delayed * mix;
		cursor = (cursor + 1u) % delay_line.size();
	}
}

void ApplyBloomReverb(const std::vector<float>& source, float sample_rate, float mix, std::vector<float>* samples) {
	if (samples == nullptr || samples->empty() || sample_rate <= 0.0f) {
		return;
	}
	const size_t delay_a = static_cast<size_t>(std::max(1.0f, sample_rate * 0.031f));
	const size_t delay_b = static_cast<size_t>(std::max(1.0f, sample_rate * 0.047f));
	const size_t delay_c = static_cast<size_t>(std::max(1.0f, sample_rate * 0.071f));
	std::vector<float> wet(samples->size(), 0.0f);
	std::vector<float> line_a(delay_a + 1u, 0.0f);
	std::vector<float> line_b(delay_b + 1u, 0.0f);
	std::vector<float> line_c(delay_c + 1u, 0.0f);
	size_t cursor_a = 0u;
	size_t cursor_b = 0u;
	size_t cursor_c = 0u;
	float tank = 0.0f;
	for (size_t index = 0; index < samples->size(); ++index) {
		const float a = line_a[cursor_a];
		const float b = line_b[cursor_b];
		const float c = line_c[cursor_c];
		tank = 0.62f * tank + 0.18f * (a + b + c);
		wet[index] = tank;
		line_a[cursor_a] = source[index] + b * 0.36f;
		line_b[cursor_b] = source[index] - c * 0.31f;
		line_c[cursor_c] = source[index] + a * 0.28f;
		cursor_a = (cursor_a + 1u) % line_a.size();
		cursor_b = (cursor_b + 1u) % line_b.size();
		cursor_c = (cursor_c + 1u) % line_c.size();
	}
	MixDryWetConstant(samples->data(), wet.data(), samples->size(), mix, samples->data());
}

void ApplyAdvancedChain(const std::string& normalized_name, float sample_rate, std::vector<float>* work) {
	if (work == nullptr || work->empty()) {
		return;
	}
	const std::vector<float> dry = *work;
	if (normalized_name == "pulteclowend") {
		ApplyTiltToneInPlace(work, 1.18f, 1.03f, 155.0f, sample_rate);
		SoftClipInPlace(work, 1.16f, 0.92f);
		ApplyCompressorInPlace(work, -7.5f, 1.45f, 0.025f, 0.003f, 0.7f);
	} else if (normalized_name == "sslbuscomp") {
		ApplyCompressorInPlace(work, -12.0f, 3.7f, 0.012f, 0.0014f, 1.9f);
		ApplyTiltToneInPlace(work, 1.03f, 1.06f, 2600.0f, sample_rate);
	} else if (normalized_name == "transientpunch") {
		ApplyTransientFocusInPlace(work, 10.0f);
		ApplyTiltToneInPlace(work, 1.07f, 1.08f, 2200.0f, sample_rate);
		ApplyCompressorInPlace(work, -4.5f, 1.25f, 0.05f, 0.002f, 0.4f);
	} else if (normalized_name == "exciterair") {
		AddHighExciter(work, 0.16f, sample_rate);
		ApplyTiltToneInPlace(work, 0.96f, 1.12f, 7200.0f, sample_rate);
	} else if (normalized_name == "la2avocal") {
		ApplyCompressorInPlace(work, -18.0f, 3.0f, 0.018f, 0.0009f, 3.2f);
		ApplyTiltToneInPlace(work, 1.04f, 1.10f, 3600.0f, sample_rate);
		AddHighExciter(work, 0.06f, sample_rate);
	} else if (normalized_name == "radioannouncer") {
		ApplyTiltToneInPlace(work, 1.20f, 0.92f, 220.0f, sample_rate);
		ApplyCompressorInPlace(work, -20.0f, 6.0f, 0.035f, 0.002f, 4.0f);
		SoftClipInPlace(work, 1.55f, 0.78f);
	} else if (normalized_name == "silkydeesser") {
		std::vector<float> sibilance = *work;
		ApplyTiltToneInPlace(&sibilance, 0.0f, 1.0f, 6200.0f, sample_rate);
		ApplyCompressorInPlace(&sibilance, -23.0f, 7.0f, 0.22f, 0.004f, 0.0f);
		AddScaledBuffer(sibilance.data(), sibilance.size(), -0.24f, work->data());
		AddHighExciter(work, 0.04f, sample_rate);
	} else if (normalized_name == "tapemachine") {
		ApplyTiltToneInPlace(work, 1.12f, 0.86f, 4300.0f, sample_rate);
		ApplyModulatedDelayBlend(dry, 23.0f, 7.0f, 0.46f, sample_rate, 0.08f, work);
		SoftClipInPlace(work, 1.34f, 0.82f);
		ApplyCompressorInPlace(work, -8.0f, 1.8f, 0.025f, 0.001f, 1.2f);
	} else if (normalized_name == "vinylmaster") {
		ApplyTiltToneInPlace(work, 0.92f, 0.91f, 7200.0f, sample_rate);
		ApplyTiltToneInPlace(work, 0.96f, 1.06f, 900.0f, sample_rate);
		SoftClipInPlace(work, 1.18f, 0.88f);
	} else if (normalized_name == "springtank") {
		SoftClipInPlace(work, 1.22f, 0.86f);
		ApplyDelayBlend(dry, static_cast<size_t>(std::max(1.0f, sample_rate * 0.017f)), 0.32f, 0.20f, work);
		ApplyBloomReverb(dry, sample_rate, 0.36f, work);
		ApplyTiltToneInPlace(work, 0.92f, 1.18f, 2600.0f, sample_rate);
	} else if (normalized_name == "dimensionchorus") {
		ApplyModulatedDelayBlend(dry, 180.0f, 44.0f, 0.24f, sample_rate, 0.32f, work);
		ApplyModulatedDelayBlend(dry, 112.0f, 26.0f, 0.41f, sample_rate, 0.20f, work);
		ApplyTiltToneInPlace(work, 0.98f, 1.06f, 6800.0f, sample_rate);
	} else if (normalized_name == "spaceecho") {
		SoftClipInPlace(work, 1.24f, 0.84f);
		ApplyDelayBlend(dry, static_cast<size_t>(std::max(1.0f, sample_rate * 0.145f)), 0.38f, 0.30f, work);
		ApplyDelayBlend(dry, static_cast<size_t>(std::max(1.0f, sample_rate * 0.312f)), 0.44f, 0.25f, work);
		ApplyTiltToneInPlace(work, 1.02f, 0.82f, 5600.0f, sample_rate);
		ApplyBloomReverb(dry, sample_rate, 0.18f, work);
	} else if (normalized_name == "shimmerbloom") {
		std::vector<float> shimmer = dry;
		ApplyModulatedDelayBlend(dry, 48.0f, 16.0f, 0.18f, sample_rate, 0.70f, &shimmer);
		ApplyGainInPlace(&shimmer, 0.35f);
		ApplyBloomReverb(shimmer, sample_rate, 0.62f, work);
		AddHighExciter(work, 0.10f, sample_rate);
	} else if (normalized_name == "psychowidener") {
		ApplyModulatedDelayBlend(dry, 96.0f, 30.0f, 0.37f, sample_rate, 0.24f, work);
		ApplyDelayBlend(dry, 118u, 0.04f, 0.12f, work);
		ApplyTiltToneInPlace(work, 0.98f, 1.04f, 1800.0f, sample_rate);
	}
	LimitBufferInPlace(work, 0.96f);
}

void FillReport(
	const CustomEffectPackage& package,
	const std::vector<float>& output,
	const CustomEffectAccelerationInfo& accel,
	CustomEffectReport* report_out) {
	if (report_out == nullptr) {
		return;
	}
	CustomEffectReport report;
	report.label = package.label;
	report.node_count = package.nodes.size();
	report.stage_count = 1u;
	report.worker_count_used = 1u;
	report.stage_reports.push_back("advanced_processing,backend=" + accel.backend_tag);
	report.node_reports.push_back(package.label + ":advanced_effect_processing=" + accel.backend_tag);
	ComputeCustomEffectReportStats(output, &report);
	*report_out = report;
}

}  // namespace

bool TryRunAdvancedCustomEffectPackage(
	const CustomEffectPackage& package,
	float sample_rate,
	const std::vector<float>& input,
	std::vector<float>* output,
	CustomEffectReport* report_out,
	bool* handled_out,
	std::string* error_out) {
	if (handled_out != nullptr) {
		*handled_out = false;
	}
	const std::string normalized_name = NormalizeName(package.label);
	if (!IsAdvancedEffectName(normalized_name)) {
		return false;
	}
	if (handled_out != nullptr) {
		*handled_out = true;
	}
	if (output == nullptr || sample_rate <= 0.0f) {
		if (error_out != nullptr) {
			*error_out = "advanced custom effect processing target is invalid";
		}
		return false;
	}
	std::vector<float> work = input;
	const CustomEffectAccelerationInfo accel = ResolveCustomEffectAcceleration();
	ApplyAdvancedChain(normalized_name, sample_rate, &work);
	*output = std::move(work);
	FillReport(package, *output, accel, report_out);
	return true;
}

}  // namespace XER::Engine::Audio::FX