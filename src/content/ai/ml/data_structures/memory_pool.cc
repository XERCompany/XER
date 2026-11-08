#include "memory_pool.h"

#include <algorithm>

namespace XER::ML::DataStructures {

MemoryPool::MemoryPool(size_t block_size, size_t initial_blocks)
		: block_size_(std::max<size_t>(1U, block_size)) {
	Grow(std::max<size_t>(1U, initial_blocks));
}

MemoryPool::~MemoryPool() = default;

void* MemoryPool::Allocate() {
	if (free_indices_.empty()) {
		Grow(std::max<size_t>(1U, blocks_.size()));
	}
	const size_t index = free_indices_.back();
	free_indices_.pop_back();
	return blocks_[index];
}

void MemoryPool::Release(void* ptr) {
	if (ptr == nullptr) {
		return;
	}

	for (size_t i = 0; i < blocks_.size(); ++i) {
		if (blocks_[i] == ptr) {
			if (std::find(free_indices_.begin(), free_indices_.end(), i) == free_indices_.end()) {
				free_indices_.push_back(i);
			}
			return;
		}
	}
}

void MemoryPool::Grow(size_t blocks) {
	const size_t old_size = raw_.size();
	const size_t append = blocks * block_size_;
	raw_.resize(old_size + append, 0U);

	const size_t old_blocks = blocks_.size();
	blocks_.resize(old_blocks + blocks);
	for (size_t i = 0; i < blocks; ++i) {
		const size_t block_index = old_blocks + i;
		blocks_[block_index] = raw_.data() + old_size + i * block_size_;
		free_indices_.push_back(block_index);
	}
}

}  // namespace XER::ML::DataStructures

