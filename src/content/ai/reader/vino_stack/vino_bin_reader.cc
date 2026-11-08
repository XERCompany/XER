#include "vino_bin_reader.h"

#include <cstring>
#include <fstream>

namespace XER::ModelsBuilder::Reader::Vino {

bool VinoBinReader::LoadFile(const std::string& filepath) {
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  data_.resize(static_cast<size_t>(size));
  return static_cast<bool>(
      file.read(reinterpret_cast<char*>(data_.data()), size));
}

std::vector<float> VinoBinReader::ReadFloats(size_t byte_offset,
                                              size_t count) const {
  size_t bytes = count * sizeof(float);
  if (byte_offset + bytes > data_.size()) return {};

  std::vector<float> out(count);
  std::memcpy(out.data(), data_.data() + byte_offset, bytes);
  return out;
}

std::vector<int8_t> VinoBinReader::ReadInt8(size_t byte_offset,
                                             size_t count) const {
  if (byte_offset + count > data_.size()) return {};
  return std::vector<int8_t>(
      reinterpret_cast<const int8_t*>(data_.data() + byte_offset),
      reinterpret_cast<const int8_t*>(data_.data() + byte_offset + count));
}

}  // namespace XER::ModelsBuilder::Reader::Vino
