#include "buffer_queue.h"

namespace XER::ML::DataStructures {

void BufferQueue::Push(std::vector<uint8_t> buffer) {
	queue_.Push(std::move(buffer));
}

std::optional<std::vector<uint8_t>> BufferQueue::TryPop() {
	return queue_.TryPop();
}

std::vector<uint8_t> BufferQueue::WaitAndPop() {
	return queue_.WaitAndPop();
}

size_t BufferQueue::Size() const {
	return queue_.Size();
}

bool BufferQueue::Empty() const {
	return queue_.Empty();
}

void BufferQueue::Clear() {
	queue_.Clear();
}

}  // namespace XER::ML::DataStructures

