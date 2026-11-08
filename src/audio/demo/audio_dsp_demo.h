#pragma once

#include <filesystem>
#include <string>

namespace XER::Audio::Demo {

struct AudioDSPDemoOptions {
	std::filesystem::path output_dir;
	std::filesystem::path input_path;
	std::string processor;
	float phase_vocoder_ratio = 1.0f;
	std::string spectral_shaper_profile = "tilt";
	int raw_sample_rate = 44100;
	int target_sample_rate = 44100;
};

bool RunAudioDSPDemo(
	const AudioDSPDemoOptions& options,
	std::string* report_out,
	std::string* error_out = nullptr);

bool RunAudioDSPDemo(
	const std::filesystem::path& output_dir,
	std::string* report_out,
	std::string* error_out = nullptr);

}  // namespace XER::Audio::Demo