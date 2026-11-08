#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Vino {

/// @brief Reads the OpenVINO .bin weights file (flat float32 buffer)
class VinoBinReader {
 public:
  /// Load .bin file into memory
  bool LoadFile(const std::string& filepath);

  /// Access raw weight bytes
  const std::vector<uint8_t>& GetData() const { return data_; }

  /// Read float slice [offset, offset+count)
  std::vector<float> ReadFloats(size_t byte_offset, size_t count) const;

  /// Read int8 slice
  std::vector<int8_t> ReadInt8(size_t byte_offset, size_t count) const;

  /// Total bytes loaded
  size_t GetSize() const { return data_.size(); }

 private:
  std::vector<uint8_t> data_;
};

}  // namespace XER::ModelsBuilder::Reader::Vino
