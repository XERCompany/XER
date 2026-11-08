#include "anal_spectrogram_generator.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace XER::Audio::AnalysisAI {

SpectrogramGenerator::SpectrogramGenerator(size_t fft_size, size_t hop_size)
	: fft_size_(fft_size),
	  hop_size_(hop_size),
	  stft_processor_(fft_size, hop_size),
	  buffer_pool_(65536, 2) {
	perf_counter_.Enable();
}

SpectrogramGenerator::~SpectrogramGenerator() = default;

bool SpectrogramGenerator::GenerateFromFile(
	const XER::Audio::Core::AudioSourceLoadOptions& load_options,
	std::string* error_out) {
	XER::Audio::Core::AudioSourceBuffer source_buffer;
	if (!XER::Audio::Core::AudioSourceLoader::Load(load_options, &source_buffer, error_out)) {
		return false;
	}
	return Generate(source_buffer.samples.data(), source_buffer.samples.size());
}

bool SpectrogramGenerator::Generate(const float* audio, size_t frame_count) {
	if (audio == nullptr || frame_count < fft_size_) {
		return false;
	}

	perf_counter_.StartCounter("spectrogram_generate");
	magnitude_matrix_.clear();
	stft_processor_.Reset();

	for (size_t offset = 0; offset + fft_size_ <= frame_count; offset += hop_size_) {
		if (!stft_processor_.AnalyzeFrame(audio + offset, fft_size_)) {
			continue;
		}

		const auto& spectrum = stft_processor_.GetSpectrum();
		const size_t positive_bins = stft_processor_.GetPositiveBinCount();
		std::vector<float> magnitude(positive_bins, 0.0f);
		for (size_t bin = 0; bin < positive_bins; ++bin) {
			magnitude[bin] = std::abs(spectrum[bin]);
		}
		magnitude_matrix_.push_back(std::move(magnitude));
	}

	perf_counter_.StopCounter("spectrogram_generate");
	return !magnitude_matrix_.empty();
}

bool SpectrogramGenerator::ExportAsRaw(const std::string& filepath) const {
	if (magnitude_matrix_.empty()) {
		return false;
	}

	std::vector<float> flat;
	for (const auto& row : magnitude_matrix_) {
		flat.insert(flat.end(), row.begin(), row.end());
	}

	return XER::AsyncIO::IO::LogDebug::DumpHelper::DumpMemoryToFile(
		filepath,
		reinterpret_cast<const uint8_t*>(flat.data()),
		flat.size() * sizeof(float));
}

void SpectrogramGenerator::Reset() {
	magnitude_matrix_.clear();
	stft_processor_.Reset();
}

std::string SpectrogramGenerator::GetReport() const {
	return "Spectrogram: frames=" + std::to_string(GetFrameCount()) +
		", bins=" + std::to_string(GetBinCount());
}

}  // namespace XER::Audio::AnalysisAI
