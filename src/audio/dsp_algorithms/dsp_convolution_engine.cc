#include "dsp_convolution_engine.h"

#include <algorithm>
#include <future>
#include <thread>

#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#endif

namespace XER::Engine::Audio::DSP {

std::string ConvolutionEngine::GetMemoryStats() const {
	std::string stats;
	stats += "ImpulseResponse: " + std::to_string(impulse_response_.size() * sizeof(float)) + " bytes\n";
	stats += "History: " + std::to_string(history_.size() * sizeof(float)) + " bytes\n";
	stats += "BufferPool blocks: " + std::to_string(buffer_pool_.GetAvailableBlocks()) + "/" + std::to_string(buffer_pool_.GetTotalBlocks()) + "\n";
	return stats;
}

ConvolutionEngine::ConvolutionEngine()
	: buffer_pool_(65536, 2) {
	perf_counter_.Enable();
}

ConvolutionEngine::~ConvolutionEngine() = default;

bool ConvolutionEngine::SetImpulseResponse(const float* ir, size_t ir_size) {
	if (ir == nullptr || ir_size == 0) {
		return false;
	}

	impulse_response_.assign(ir, ir + ir_size);
	history_.assign(ir_size, 0.0f);
	history_cursor_ = 0;
	return true;
}

bool ConvolutionEngine::ProcessBlock(const float* input, size_t frame_count, float* output) {
	if (input == nullptr || output == nullptr || frame_count == 0 || impulse_response_.empty()) {
		return false;
	}

	perf_counter_.StartCounter("convolution_block");

	auto tmp = buffer_pool_.AcquireBuffer();
	if (tmp != nullptr) {
		tmp->used_bytes = 0;
		buffer_pool_.ReleaseBuffer(tmp);
	}

	const size_t history_size = history_.size();

	{
#if defined(__AVX2__)
		const size_t ir_size = impulse_response_.size();
		for (size_t n = 0; n < frame_count; ++n) {
			history_[history_cursor_] = input[n];
			float acc = 0.0f;
			size_t history_index = history_cursor_;
			size_t k = 0;
			for (; k + 7 < ir_size; k += 8) {
				__m256 ir = _mm256_loadu_ps(&impulse_response_[k]);
				float h[8];
				for (int j = 0; j < 8; ++j) {
					h[j] = history_[(history_index + history_size - (k + static_cast<size_t>(j))) % history_size];
				}
				__m256 hist = _mm256_loadu_ps(h);
				__m256 mul = _mm256_mul_ps(ir, hist);
				float sum[8];
				_mm256_storeu_ps(sum, mul);
				for (int j = 0; j < 8; ++j) acc += sum[j];
			}
			for (; k < ir_size; ++k) {
				acc += impulse_response_[k] * history_[(history_index + history_size - k) % history_size];
			}
			output[n] = acc;
			history_cursor_ = (history_cursor_ + 1) % history_size;
		}
#elif defined(__SSE2__)
		const size_t ir_size = impulse_response_.size();
		for (size_t n = 0; n < frame_count; ++n) {
			history_[history_cursor_] = input[n];
			float acc = 0.0f;
			size_t history_index = history_cursor_;
			size_t k = 0;
			for (; k + 3 < ir_size; k += 4) {
				__m128 ir = _mm_loadu_ps(&impulse_response_[k]);
				float h[4];
				for (int j = 0; j < 4; ++j) {
					h[j] = history_[(history_index + history_size - (k + static_cast<size_t>(j))) % history_size];
				}
				__m128 hist = _mm_loadu_ps(h);
				__m128 mul = _mm_mul_ps(ir, hist);
				float sum[4];
				_mm_storeu_ps(sum, mul);
				for (int j = 0; j < 4; ++j) acc += sum[j];
			}
			for (; k < ir_size; ++k) {
				acc += impulse_response_[k] * history_[(history_index + history_size - k) % history_size];
			}
			output[n] = acc;
			history_cursor_ = (history_cursor_ + 1) % history_size;
		}
#else
		for (size_t n = 0; n < frame_count; ++n) {
			history_[history_cursor_] = input[n];
			float acc = 0.0f;
			size_t history_index = history_cursor_;
			for (size_t k = 0; k < impulse_response_.size(); ++k) {
				acc += impulse_response_[k] * history_[history_index];
				history_index = (history_index == 0) ? (history_size - 1) : (history_index - 1);
			}
			output[n] = acc;
			history_cursor_ = (history_cursor_ + 1) % history_size;
		}
#endif
		perf_counter_.StopCounter("convolution_block");
		return true;
	}
}

void ConvolutionEngine::Reset() {
	std::fill(history_.begin(), history_.end(), 0.0f);
	history_cursor_ = 0;
}

std::string ConvolutionEngine::GetReport() const {
	return "Convolution: ir_size=" + std::to_string(impulse_response_.size());
}

std::future<bool> ConvolutionEngine::ProcessBlockAsync(const float* input, size_t frame_count, float* output) {
	return std::async(std::launch::async, [this, input, frame_count, output]() {
		return this->ProcessBlock(input, frame_count, output);
	});
}

}  // namespace XER::Engine::Audio::DSP
