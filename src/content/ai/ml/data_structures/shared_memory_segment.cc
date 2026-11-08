#include "shared_memory_segment.h"

namespace XER::ML::DataStructures {

bool SharedMemorySegment::Create(const std::string& name, size_t size) {
	name_ = name;
	buffer_.assign(size, 0U);
	return true;
}

bool SharedMemorySegment::Attach(const std::string& name, size_t size) {
	name_ = name;
	if (buffer_.size() != size) {
		buffer_.assign(size, 0U);
	}
	return true;
}

void SharedMemorySegment::Detach() {
	buffer_.clear();
	name_.clear();
}

uint8_t* SharedMemorySegment::Data() {
	return buffer_.empty() ? nullptr : buffer_.data();
}

const uint8_t* SharedMemorySegment::Data() const {
	return buffer_.empty() ? nullptr : buffer_.data();
}

}  // namespace XER::ML::DataStructures

