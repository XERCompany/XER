#include "custom_effect_apply_multi.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "async_io/io_thread_pool.h"
#include "custom_effect_accel.h"

namespace Engine::Audio::FX {

bool ApplyCustomEffectProcessors(
	std::deque<CustomEffectProcessor>* processors,
	const std::vector<float>& input,
	const CustomEffectRenderConfig& render_config,
	std::vector<float>* output,
	size_t* worker_count_used,
	std::string* error_out) {
	if (processors == nullptr || processors->empty() || output == nullptr || render_config.block_size == 0) {
		if (error_out != nullptr) {
			*error_out = "custom effect processor stage is invalid";
		}
		return false;
	}

	std::vector<std::vector<float>> branch_outputs(processors->size());
	if (worker_count_used != nullptr) {
		*worker_count_used = 1;
	}

	auto process_branch = [&input, &render_config, error_out](CustomEffectProcessor* processor, std::vector<float>* branch_output) {
		std::string local_error;
		const bool ok = processor->ProcessBuffer(input, render_config.block_size, branch_output, &local_error);
		if (!ok && error_out != nullptr && error_out->empty()) {
			*error_out = local_error;
		}
		return ok;
	};

	const bool run_parallel = render_config.enable_multicore_render && processors->size() > 1u;
	if (!run_parallel) {
		for (size_t index = 0; index < processors->size(); ++index) {
			if (!process_branch(&(*processors)[index], &branch_outputs[index])) {
				return false;
			}
		}
	} else {
		AsyncIO::IO::Async::IOThreadPool& pool = AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();
		std::mutex state_mutex;
		std::condition_variable state_cv;
		size_t remaining = processors->size();
		bool all_ok = true;
		std::string first_error;
		for (size_t index = 0; index < processors->size(); ++index) {
			pool.Enqueue([&, index]() {
				std::string local_error;
				const bool ok = (*processors)[index].ProcessBuffer(input, render_config.block_size, &branch_outputs[index], &local_error);
				{
					std::lock_guard<std::mutex> lock(state_mutex);
					if (!ok) {
						all_ok = false;
						if (first_error.empty()) {
							first_error = local_error;
						}
					}
					--remaining;
				}
				state_cv.notify_one();
			});
		}
		std::unique_lock<std::mutex> lock(state_mutex);
		state_cv.wait(lock, [&remaining]() {
			return remaining == 0;
		});
		if (!all_ok) {
			if (error_out != nullptr) {
				*error_out = first_error.empty() ? "custom effect parallel stage failed" : first_error;
			}
			return false;
		}
		if (worker_count_used != nullptr) {
			*worker_count_used = std::min(processors->size(), pool.GetThreadCount());
		}
	}

	SumNormalizeBuffers(branch_outputs, output);
	return true;
}

}  // namespace Engine::Audio::FX
