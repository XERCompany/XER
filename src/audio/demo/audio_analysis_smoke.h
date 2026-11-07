#pragma once

#include <filesystem>
#include <string>

namespace XER::Engine::Audio::Demo {

struct AudioAnalysisSmokeOptions {
	std::filesystem::path input_path;
	std::filesystem::path output_dir;
	int raw_sample_rate = 44100;
	int target_sample_rate = 44100;
	int max_cpu_threads = -1;
	bool write_artifacts = true;
};

bool RunAudioAnalysisSmoke(
	const AudioAnalysisSmokeOptions& options,
	std::string* report_out,
	std::string* error_out = nullptr);

}  // namespace XER::Engine::Audio::Demo