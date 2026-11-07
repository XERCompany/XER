// Пример использования:
// AudioModulesSmokeOptions opts;
// opts.input_path = "test.mp3";
// opts.output_dir = "./smoke_artifacts";
// std::string report, error;
// bool ok = RunAudioModulesSmoke(opts, &report, &error);
// if (!ok) std::cerr << error << std::endl;
// else std::cout << report << std::endl;
#pragma once

#include <filesystem>
#include <string>

namespace XER::Engine::Audio::Demo {

struct AudioModulesSmokeOptions {
	std::filesystem::path input_path;
	std::filesystem::path output_dir;
};

bool RunAudioModulesSmoke(
	const AudioModulesSmokeOptions& options,
	std::string* report,
	std::string* error_out = nullptr);

}  // namespace XER::Engine::Audio::Demo