#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "async_io/async_buffer_pool.h"
#include "async_io/log_and_debug/io_perf_counter.h"
#include "async_io/sync_primitives/mutex_wrapper.h"

namespace Engine::Audio::AnalysisAI {

class LoudnessMeterLUFS {
public:
	LoudnessMeterLUFS();
	~LoudnessMeterLUFS();

	bool ProcessBlock(const float* interleaved_audio, size_t frame_count, int channels);
	void Reset();

	double GetMomentaryLUFS() const { return momentary_lufs_; }
	double GetShortTermLUFS() const { return short_term_lufs_; }
	double GetIntegratedLUFS() const { return integrated_lufs_; }
	double GetTruePeakDBFS() const { return true_peak_dbfs_; }
	uint64_t GetProcessedBlocks() const { return processed_blocks_; }
	const std::vector<double>& GetHistory() const { return loudness_history_; }
	std::string GetReport() const;

private:
	AsyncIO::IO::Async::AsyncBufferPool buffer_pool_;
	AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
	AsyncIO::IO::Sync::MutexWrapper mutex_;
	std::vector<double> loudness_history_;
	double momentary_lufs_;
	double short_term_lufs_;
	double integrated_lufs_;
	double true_peak_dbfs_;
	uint64_t processed_blocks_;

	static double MeanSquareToLUFS(double mean_square);
};

}  // namespace Engine::Audio::AnalysisAI
