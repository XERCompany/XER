#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace XER::ML::DataStructures {

class SharedMemorySegment {
 public:
	SharedMemorySegment() = default;

	bool Create(const std::string& name, size_t size);
	bool Attach(const std::string& name, size_t size);
	void Detach();

	uint8_t* Data();
	const uint8_t* Data() const;
	size_t Size() const { return buffer_.size(); }
	const std::string& Name() const { return name_; }

 private:
	std::string name_;
	std::vector<uint8_t> buffer_;
};

}  // namespace XER::ML::DataStructures

