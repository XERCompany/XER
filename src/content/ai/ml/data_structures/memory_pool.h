#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::ML::DataStructures {

class MemoryPool {
 public:
	MemoryPool(size_t block_size, size_t initial_blocks = 32U);
	~MemoryPool();

	void* Allocate();
	void Release(void* ptr);

	size_t BlockSize() const { return block_size_; }
	size_t CapacityBlocks() const { return blocks_.size(); }
	size_t FreeBlocks() const { return free_indices_.size(); }

 private:
	void Grow(size_t blocks);

	size_t block_size_;
	std::vector<uint8_t> raw_;
	std::vector<void*> blocks_;
	std::vector<size_t> free_indices_;
};

}  // namespace XER::ML::DataStructures

