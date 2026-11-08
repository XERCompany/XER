#include "audio/demo/audio_analysis_smoke.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void PrintHelp(const char* binary_name) {
	std::cout
		<< "Usage: " << binary_name << " [audio_analysis_smoke] <input_audio> [options]\n"
		<< "Options:\n"
		<< "  --audio_input <path>         Audio file to analyze\n"
		<< "  --output_dir <dir>           Directory for report artifacts\n"
		<< "  --target_sample_rate <hz>    Analysis sample rate, default 44100\n"
		<< "  --audio_raw_sample_rate <hz> Raw PCM sample rate, default 44100\n"
		<< "  --max_cpu_threads <n>        Limit XER worker CPU threads\n"
		<< "  --skip_artifacts             Do not write report/CSV artifacts\n"
		<< "  --version                    Print version\n"
		<< "  help                         Print this help\n";
}

bool ParseInt(const std::string& value, int* output) {
	if (output == nullptr) {
		return false;
	}
	char* end = nullptr;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str() || *end != '\0' || parsed <= 0) {
		return false;
	}
	*output = static_cast<int>(parsed);
	return true;
}

}  // namespace

int main(int argc, char** argv) {
	const char* binary_name = argc > 0 && argv[0] != nullptr ? argv[0] : "XERAudioAnalysisSmoke";
	if (argc <= 1) {
		PrintHelp(binary_name);
		return 0;
	}

	XER::Audio::Demo::AudioAnalysisSmokeOptions options;
	std::string error;

	for (int index = 1; index < argc; ++index) {
		const std::string arg = argv[index] != nullptr ? argv[index] : "";
		if (arg == "audio_analysis_smoke") {
			continue;
		}
		if (arg == "--skip_artifacts" || arg == "--no_artifacts") {
			options.write_artifacts = false;
			continue;
		}
		if (arg == "help" || arg == "--help" || arg == "-h") {
			PrintHelp(binary_name);
			return 0;
		}
		if (arg == "version" || arg == "--version" || arg == "-v") {
			std::cout << "XERAudioAnalysisSmoke 1.0\n";
			return 0;
		}
		if ((arg == "--audio_input" || arg == "--input") && index + 1 < argc) {
			options.input_path = argv[++index];
			continue;
		}
		if (arg == "--output_dir" && index + 1 < argc) {
			options.output_dir = argv[++index];
			continue;
		}
		if (arg == "--target_sample_rate" && index + 1 < argc) {
			if (!ParseInt(argv[++index], &options.target_sample_rate)) {
				std::cerr << "Invalid --target_sample_rate value\n";
				return 2;
			}
			continue;
		}
		if (arg == "--audio_raw_sample_rate" && index + 1 < argc) {
			if (!ParseInt(argv[++index], &options.raw_sample_rate)) {
				std::cerr << "Invalid --audio_raw_sample_rate value\n";
				return 2;
			}
			continue;
		}
		if ((arg == "--max_cpu_threads" || arg == "--max-threads-cpu") && index + 1 < argc) {
			if (!ParseInt(argv[++index], &options.max_cpu_threads)) {
				std::cerr << "Invalid --max_cpu_threads value\n";
				return 2;
			}
			const std::string threads = std::to_string(options.max_cpu_threads);
			#if defined(_WIN32)
			_putenv_s("ENGINE_MAX_CPU_THREADS", threads.c_str());
			_putenv_s("ENGINE_ASYNC_IO_WORKERS", threads.c_str());
			#else
			setenv("ENGINE_MAX_CPU_THREADS", threads.c_str(), 1);
			setenv("ENGINE_ASYNC_IO_WORKERS", threads.c_str(), 1);
			setenv("OMP_NUM_THREADS", threads.c_str(), 1);
			setenv("FFTW_NUM_THREADS", threads.c_str(), 1);
			#endif
			continue;
		}
		if (!arg.empty() && arg.front() != '-' && options.input_path.empty()) {
			options.input_path = arg;
			continue;
		}
		std::cerr << "Unknown argument: " << arg << "\n";
		return 2;
	}

	if (options.output_dir.empty()) {
		options.output_dir = std::filesystem::path("out") / "audio_analysis_smoke";
	}

	std::string report;
	if (!XER::Audio::Demo::RunAudioAnalysisSmoke(options, &report, &error)) {
		std::cerr << error << "\n";
		return 1;
	}

	std::cout << report;
	if (report.empty() || report.back() != '\n') {
		std::cout << '\n';
	}
	return 0;
}