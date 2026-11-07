#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "async_io/async_buffer_pool.h"
#include "async_io/log_and_debug/io_dump_helper.h"
#include "async_io/log_and_debug/io_perf_counter.h"

#include "audio/audio_core/audio_source_loader.h"
#include "audio/dsp_algorithms/dsp_stft_processor.h"

namespace XER::Engine::Audio::AnalysisAI {

class SpectrogramGenerator {
public:
	SpectrogramGenerator(size_t fft_size = 1024, size_t hop_size = 256);
	~SpectrogramGenerator();

	bool Generate(const float* audio, size_t frame_count);
	bool GenerateFromFile(const XER::Engine::Audio::Core::AudioSourceLoadOptions& load_options, std::string* error_out = nullptr);
	bool ExportAsRaw(const std::string& filepath) const;
	void Reset();

	const std::vector<std::vector<float>>& GetMagnitudeMatrix() const { return magnitude_matrix_; }
	size_t GetFrameCount() const { return magnitude_matrix_.size(); }
	size_t GetBinCount() const { return (fft_size_ / 2) + 1; }
	std::string GetReport() const;

private:
	size_t fft_size_;
	size_t hop_size_;
	XER::Engine::Audio::DSP::STFTProcessor stft_processor_;
	XER::AsyncIO::IO::Async::AsyncBufferPool buffer_pool_;
	XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
	std::vector<std::vector<float>> magnitude_matrix_;
};

}  // namespace XER::Engine::Audio::AnalysisAI
