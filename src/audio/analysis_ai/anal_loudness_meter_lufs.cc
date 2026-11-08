#include "anal_loudness_meter_lufs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace XER::Audio::AnalysisAI {

LoudnessMeterLUFS::LoudnessMeterLUFS()
	: buffer_pool_(65536, 2),
	  mutex_("loudness_meter_lufs"),
	  momentary_lufs_(0.0),
	  short_term_lufs_(0.0),
	  integrated_lufs_(0.0),
	  true_peak_dbfs_(0.0),
	  processed_blocks_(0) {
	perf_counter_.Enable();
}

LoudnessMeterLUFS::~LoudnessMeterLUFS() = default;

bool LoudnessMeterLUFS::ProcessBlock(const float* interleaved_audio, size_t frame_count, int channels) {
	if (interleaved_audio == nullptr || frame_count == 0 || channels <= 0) {
		return false;
	}

	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	perf_counter_.StartCounter("lufs_process");

	const size_t sample_count = frame_count * static_cast<size_t>(channels);
	double mean_square = 0.0;
	double peak = 0.0;

	for (size_t index = 0; index < sample_count; ++index) {
		const double sample = interleaved_audio[index];
		mean_square += sample * sample;
		peak = std::max(peak, std::abs(sample));
	}

	mean_square /= static_cast<double>(sample_count);
	momentary_lufs_ = MeanSquareToLUFS(mean_square);
	true_peak_dbfs_ = 20.0 * std::log10(std::max(peak, 1e-9));

	loudness_history_.push_back(momentary_lufs_);
	if (loudness_history_.size() > 120) {
		loudness_history_.erase(loudness_history_.begin());
	}

	const size_t short_count = std::min<size_t>(15, loudness_history_.size());
	short_term_lufs_ = std::accumulate(loudness_history_.end() - static_cast<std::ptrdiff_t>(short_count),
		loudness_history_.end(), 0.0) / static_cast<double>(short_count);

	integrated_lufs_ = std::accumulate(loudness_history_.begin(), loudness_history_.end(), 0.0) /
		static_cast<double>(loudness_history_.size());

	auto buffer = buffer_pool_.AcquireBuffer();
	if (buffer != nullptr && !buffer->data.empty()) {
		const size_t copy_bytes = std::min(buffer->data.size(), sample_count * sizeof(float));
		std::memcpy(buffer->data.data(), interleaved_audio, copy_bytes);
		buffer->used_bytes = copy_bytes;
		buffer_pool_.ReleaseBuffer(buffer);
	}

	++processed_blocks_;
	perf_counter_.StopCounter("lufs_process");
	return true;
}

void LoudnessMeterLUFS::Reset() {
	XER::AsyncIO::IO::Sync::MutexWrapper::ScopedLock lock(mutex_);
	loudness_history_.clear();
	momentary_lufs_ = 0.0;
	short_term_lufs_ = 0.0;
	integrated_lufs_ = 0.0;
	true_peak_dbfs_ = 0.0;
	processed_blocks_ = 0;
}

std::string LoudnessMeterLUFS::GetReport() const {
	return "LUFS: momentary=" + std::to_string(momentary_lufs_) +
		", short=" + std::to_string(short_term_lufs_) +
		", integrated=" + std::to_string(integrated_lufs_) +
		", true_peak_dbfs=" + std::to_string(true_peak_dbfs_) +
		", blocks=" + std::to_string(processed_blocks_);
}

double LoudnessMeterLUFS::MeanSquareToLUFS(double mean_square) {
	return -0.691 + 10.0 * std::log10(std::max(mean_square, 1e-12));
}

}  // namespace AIToolsXPro::Audio::AnalysisAI
