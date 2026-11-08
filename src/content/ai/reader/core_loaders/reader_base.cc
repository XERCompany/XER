#include "reader_base.h"

#include <fstream>

namespace XER::ModelsBuilder::Reader {

bool ReaderBase::ReadFile(const std::string& filepath, std::vector<uint8_t>& out_bytes) {
	out_bytes.clear();

	std::ifstream file(filepath, std::ios::binary);
	if (!file.is_open()) {
		return false;
	}

	file.seekg(0, std::ios::end);
	const std::streampos end = file.tellg();
	if (end < 0) {
		return false;
	}

	const size_t size = static_cast<size_t>(end);
	file.seekg(0, std::ios::beg);
	out_bytes.resize(size);
	if (size == 0U) {
		return true;
	}

	return file.read(reinterpret_cast<char*>(out_bytes.data()), static_cast<std::streamsize>(size)).good();
}

}  // namespace XER::ModelsBuilder::Reader

