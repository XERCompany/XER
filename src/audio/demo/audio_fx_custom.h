#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::Engine::Audio::Demo {

struct AudioFxCustomOptions {
	std::filesystem::path output_dir;
	std::filesystem::path input_path;
	std::string effect_name;
	std::vector<std::string> effect_names;
	std::string clap_plugin_reference = "builtin://gain";
	std::vector<std::string> clap_plugin_references;
	int raw_sample_rate = 44100;
	int target_sample_rate = 44100;
	int target_channels = -1;
	std::string batch_mode = "parallel";
	bool json_summary = false;
	bool strict_mp3_input = true;
	bool pipe_mp3_output = false;
	bool write_intermediate_wavs = true;
};

bool RunAudioFxCustom(
	const AudioFxCustomOptions& options,
	std::string* report_out,
	std::string* error_out = nullptr,
	std::vector<std::uint8_t>* mp3_output_out = nullptr);

bool RunAudioFxBatch(
	const AudioFxCustomOptions& options,
	std::string* report_out,
	std::string* error_out = nullptr,
	std::vector<std::uint8_t>* mp3_output_out = nullptr);

}  // namespace XER::Engine::Audio::Demo