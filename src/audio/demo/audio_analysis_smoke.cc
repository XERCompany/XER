#include "audio/demo/audio_analysis_smoke.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <vector>

#include "audio/analysis_ai/anal_beat_tracker.h"
#include "audio/analysis_ai/anal_loudness_meter_lufs.h"
#include "audio/analysis_ai/anal_pitch_estimator.h"
#include "audio/audio_core/audio_source_loader.h"

namespace XER::Engine::Audio::Demo {

namespace {

constexpr size_t kPitchWindowSize = 4096u;
constexpr size_t kPitchHopSize = 1024u;
constexpr size_t kLoudnessBlockSize = 2048u;

struct PitchWindowSelection {
	size_t offset = 0;
	size_t frame_count = 0;
	double rms = 0.0;
};

double SecondsFromFrames(size_t frame_count, int sample_rate) {
	if (sample_rate <= 0) {
		return 0.0;
	}
	return static_cast<double>(frame_count) / static_cast<double>(sample_rate);
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string* error_out) {
	std::ofstream output(path);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open analysis report file: " + path.string();
		}
		return false;
	}
	output << text;
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "failed to write analysis report file: " + path.string();
		}
		return false;
	}
	return true;
}

bool WriteBeatEventsCsv(
	const std::filesystem::path& path,
	const std::vector<XER::Engine::Audio::AnalysisAI::BeatEvent>& beat_events,
	std::string* error_out) {
	std::ofstream output(path);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open beat CSV file: " + path.string();
		}
		return false;
	}
	output << "frame_index,time_seconds,strength\n";
	for (const auto& beat : beat_events) {
		output << beat.frame_index << "," << beat.time_seconds << "," << beat.strength << "\n";
	}
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "failed to write beat CSV file: " + path.string();
		}
		return false;
	}
	return true;
}

bool WriteSeriesCsv(
	const std::filesystem::path& path,
	const std::vector<double>& values,
	const char* header,
	std::string* error_out) {
	std::ofstream output(path);
	if (!output.is_open()) {
		if (error_out != nullptr) {
			*error_out = "failed to open analysis series CSV file: " + path.string();
		}
		return false;
	}
	output << "index," << header << "\n";
	for (size_t index = 0; index < values.size(); ++index) {
		output << index << "," << values[index] << "\n";
	}
	if (!output) {
		if (error_out != nullptr) {
			*error_out = "failed to write analysis series CSV file: " + path.string();
		}
		return false;
	}
	return true;
}

PitchWindowSelection SelectPitchWindow(const std::vector<float>& samples) {
	PitchWindowSelection selection;
	if (samples.empty()) {
		return selection;
	}

	selection.frame_count = std::min(samples.size(), kPitchWindowSize);
	selection.rms = 0.0;
	const size_t hop_size = std::min(selection.frame_count, kPitchHopSize);
	double best_mean_square = -1.0;
	for (size_t offset = 0; offset + selection.frame_count <= samples.size(); offset += hop_size) {
		double sum_squared = 0.0;
		for (size_t index = 0; index < selection.frame_count; ++index) {
			const double sample = samples[offset + index];
			sum_squared += sample * sample;
		}
		const double mean_square = sum_squared / static_cast<double>(selection.frame_count);
		if (mean_square <= best_mean_square) {
			continue;
		}
		best_mean_square = mean_square;
		selection.offset = offset;
		selection.rms = std::sqrt(mean_square);
	}

	if (selection.frame_count == 0) {
		selection.frame_count = samples.size();
	}
	return selection;
}

}  // namespace

bool RunAudioAnalysisSmoke(
	const AudioAnalysisSmokeOptions& options,
	std::string* report_out,
	std::string* error_out) {
	if (report_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "analysis report target is null";
		}
		return false;
	}
	if (options.input_path.empty()) {
		if (error_out != nullptr) {
			*error_out = "audio_analysis_smoke requires an input file";
		}
		return false;
	}

	XER::Engine::Audio::Core::AudioSourceLoadOptions load_options;
	load_options.input_path = options.input_path;
	load_options.raw_sample_rate = options.raw_sample_rate;
	load_options.target_sample_rate = options.target_sample_rate;
	load_options.target_channels = 1;

	XER::Engine::Audio::Core::AudioSourceBuffer audio_buffer;
	if (!XER::Engine::Audio::Core::AudioSourceLoader::Load(load_options, &audio_buffer, error_out)) {
		return false;
	}
	if (audio_buffer.samples.empty() || audio_buffer.sample_rate <= 0) {
		if (error_out != nullptr) {
			*error_out = "normalized analysis buffer is empty";
		}
		return false;
	}

	XER::Engine::Audio::AnalysisAI::BeatTracker beat_tracker;
	if (!beat_tracker.Analyze(audio_buffer.samples.data(), audio_buffer.samples.size(), audio_buffer.sample_rate)) {
		if (error_out != nullptr) {
			*error_out = "beat tracker analysis failed";
		}
		return false;
	}

	const PitchWindowSelection pitch_window = SelectPitchWindow(audio_buffer.samples);
	XER::Engine::Audio::AnalysisAI::PitchEstimator pitch_estimator(audio_buffer.sample_rate);
	if (!pitch_estimator.Estimate(
			audio_buffer.samples.data() + static_cast<std::ptrdiff_t>(pitch_window.offset),
			pitch_window.frame_count)) {
		if (error_out != nullptr) {
			*error_out = "pitch estimation failed";
		}
		return false;
	}

	XER::Engine::Audio::AnalysisAI::LoudnessMeterLUFS loudness_meter;
	for (size_t cursor = 0; cursor < audio_buffer.samples.size(); cursor += kLoudnessBlockSize) {
		const size_t frames = std::min(kLoudnessBlockSize, audio_buffer.samples.size() - cursor);
		if (!loudness_meter.ProcessBlock(
				audio_buffer.samples.data() + static_cast<std::ptrdiff_t>(cursor),
				frames,
				1)) {
			if (error_out != nullptr) {
				*error_out = "loudness analysis failed";
			}
			return false;
		}
	}

	const auto& beat_events = beat_tracker.GetBeatEvents();
	const auto& pitch_info = pitch_estimator.GetLastPitch();
	const auto& loudness_history = loudness_meter.GetHistory();

	std::ostringstream output;
	output << std::fixed << std::setprecision(6);
	output << "Audio Analysis Smoke\n";
	output << "input_path=" << options.input_path.string() << "\n";
	output << "source_format=" << audio_buffer.source_format << "\n";
	output << "decode_backend=" << audio_buffer.decode_backend << "\n";
	output << "sample_rate=" << audio_buffer.sample_rate << "\n";
	output << "normalized_frames=" << audio_buffer.samples.size() << "\n";
	output << "duration_seconds=" << SecondsFromFrames(audio_buffer.samples.size(), audio_buffer.sample_rate) << "\n";
	output << "beat_count=" << beat_events.size() << "\n";
	output << "beat_estimated_bpm=" << beat_tracker.GetEstimatedBPM() << "\n";
	output << "beat_average_strength=" << beat_tracker.GetAverageStrength() << "\n";
	output << "pitch_window_offset=" << pitch_window.offset << "\n";
	output << "pitch_window_frames=" << pitch_window.frame_count << "\n";
	output << "pitch_window_rms=" << pitch_window.rms << "\n";
	output << "pitch_frequency=" << pitch_info.frequency << "\n";
	output << "pitch_confidence=" << pitch_info.confidence << "\n";
	output << "pitch_note=" << pitch_estimator.GetLastNote() << "\n";
	output << "loudness_momentary_lufs=" << loudness_meter.GetMomentaryLUFS() << "\n";
	output << "loudness_short_term_lufs=" << loudness_meter.GetShortTermLUFS() << "\n";
	output << "loudness_integrated_lufs=" << loudness_meter.GetIntegratedLUFS() << "\n";
	output << "loudness_true_peak_dbfs=" << loudness_meter.GetTruePeakDBFS() << "\n";
	output << "loudness_history_blocks=" << loudness_history.size() << "\n";
	output << "loudness_processed_blocks=" << loudness_meter.GetProcessedBlocks() << "\n";
	output << "beat_report=" << beat_tracker.GetReport() << "\n";
	output << "pitch_report=" << pitch_estimator.GetReport() << "\n";
	output << "loudness_report=" << loudness_meter.GetReport() << "\n";
	output << "smoke_status=pass\n";

	if (options.write_artifacts && !options.output_dir.empty()) {
		std::error_code fs_error;
		std::filesystem::create_directories(options.output_dir, fs_error);
		if (fs_error) {
			if (error_out != nullptr) {
				*error_out = "failed to create audio analysis output directory";
			}
			return false;
		}

		const std::filesystem::path report_path = options.output_dir / "audio_analysis_report.txt";
		const std::filesystem::path beats_csv_path = options.output_dir / "beats.csv";
		const std::filesystem::path loudness_csv_path = options.output_dir / "loudness_history.csv";
		const std::string report_text = output.str();
		std::string io_error;
		if (!WriteTextFile(report_path, report_text, &io_error)
			|| !WriteBeatEventsCsv(beats_csv_path, beat_events, &io_error)
			|| !WriteSeriesCsv(loudness_csv_path, loudness_history, "lufs", &io_error)) {
			if (error_out != nullptr) {
				*error_out = io_error;
			}
			return false;
		}
		output << "report_path=" << report_path.string() << "\n";
		output << "beats_csv=" << beats_csv_path.string() << "\n";
		output << "loudness_csv=" << loudness_csv_path.string() << "\n";
	}

	*report_out = output.str();
	return true;
}

}  // namespace XER::Engine::Audio::Demo