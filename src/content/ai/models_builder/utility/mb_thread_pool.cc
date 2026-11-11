#include "mb_thread_pool.h"

#include "../../../../async_io/io_thread_pool.h"

namespace XER::ModelsBuilder::Utility {

ModelThreadPool& ModelThreadPool::GetInstance() {
	static ModelThreadPool instance;
	return instance;
}

void ModelThreadPool::Enqueue(std::function<void()> task) {
	if (!task) {
		return;
	}

	pending_tasks_.fetch_add(1, std::memory_order_relaxed);
	XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance().Enqueue([this, task = std::move(task)]() mutable {
		task();

		const size_t remaining = pending_tasks_.fetch_sub(1, std::memory_order_acq_rel) - 1U;
		if (remaining == 0U) {
			std::lock_guard<std::mutex> lock(idle_mutex_);
			idle_cv_.notify_all();
		}
	});
}

void ModelThreadPool::WaitForIdle() {
	std::unique_lock<std::mutex> lock(idle_mutex_);
	idle_cv_.wait(lock, [this]() { return pending_tasks_.load(std::memory_order_acquire) == 0U; });
}

size_t ModelThreadPool::GetPendingTaskCount() const {
	return pending_tasks_.load(std::memory_order_acquire);
}

void ModelThreadPool::ParallelFor(size_t iterations, const std::function<void(size_t)>& task) {
	if (iterations == 0U || !task) {
		return;
	}

	for (size_t index = 0; index < iterations; ++index) {
		Enqueue([index, &task]() { task(index); });
	}

	WaitForIdle();
}

} // namespace XER::ModelsBuilder::Utility

