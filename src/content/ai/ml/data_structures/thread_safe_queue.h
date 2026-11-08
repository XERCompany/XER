#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>

namespace XER::ML::DataStructures {

template <typename T>
class ThreadSafeQueue {
 public:
	void Push(T value) {
		{
			std::scoped_lock lock(mutex_);
			queue_.push(std::move(value));
		}
		cv_.notify_one();
	}

	std::optional<T> TryPop() {
		std::scoped_lock lock(mutex_);
		if (queue_.empty()) {
			return std::nullopt;
		}
		T value = std::move(queue_.front());
		queue_.pop();
		return value;
	}

	T WaitAndPop() {
		std::unique_lock lock(mutex_);
		cv_.wait(lock, [&]() { return !queue_.empty(); });
		T value = std::move(queue_.front());
		queue_.pop();
		return value;
	}

	bool Empty() const {
		std::scoped_lock lock(mutex_);
		return queue_.empty();
	}

	size_t Size() const {
		std::scoped_lock lock(mutex_);
		return queue_.size();
	}

	void Clear() {
		std::scoped_lock lock(mutex_);
		std::queue<T> empty;
		queue_.swap(empty);
	}

 private:
	mutable std::mutex mutex_;
	std::condition_variable cv_;
	std::queue<T> queue_;
};

}  // namespace XER::ML::DataStructures

