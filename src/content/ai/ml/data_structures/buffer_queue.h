#pragma once

#include "thread_safe_queue.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace XER::ML::DataStructures {

class BufferQueue {
 public:
	void Push(std::vector<uint8_t> buffer);
	std::optional<std::vector<uint8_t>> TryPop();
	std::vector<uint8_t> WaitAndPop();

	size_t Size() const;
	bool Empty() const;
	void Clear();

 private:
	ThreadSafeQueue<std::vector<uint8_t>> queue_;
};

}  // namespace XER::ML::DataStructures

