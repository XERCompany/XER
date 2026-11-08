#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>

namespace XER::ModelsBuilder::Utility {

class ModelThreadPool {
public:
	static ModelThreadPool& GetInstance();

	void Enqueue(std::function<void()> task);
	void WaitForIdle();
	size_t GetPendingTaskCount() const;

	void ParallelFor(size_t iterations, const std::function<void(size_t)>& task);

private:
	ModelThreadPool() = default;

	mutable std::mutex idle_mutex_;
	std::condition_variable idle_cv_;
	std::atomic<size_t> pending_tasks_{0};
};

} // namespace XER::ModelsBuilder::Utility

