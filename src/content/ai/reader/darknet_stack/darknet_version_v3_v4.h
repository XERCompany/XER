#pragma once

#include <cstdint>
#include <string>

namespace XER::ModelsBuilder::Reader::Darknet {

/// @brief Darknet/YOLO model version info
struct DarknetVersion {
  uint32_t major{0};
  uint32_t minor{0};
  uint32_t revision{0};
  uint64_t seen{0};       ///< Total training images seen
  int      is_tiny{0};    ///< 1 = tiny model
  int      is_yolo_v4{0}; ///< 1 = YOLOv4+
};

/// @brief Parses and identifies Darknet v3/v4 binary weight file headers
class DarknetVersionV3V4 {
 public:
  /// Parse version header from raw weights bytes
  /// @param data  Pointer to start of .weights file
  /// @param size  Total byte count
  /// @return true if valid Darknet header found
  bool Parse(const uint8_t* data, size_t size);

  const DarknetVersion& GetVersion() const { return version_; }

  /// Byte offset where weight data begins (after header)
  size_t GetWeightOffset() const { return weight_offset_; }

  /// Human-readable version string
  std::string GetVersionString() const;

  /// Check if version is YOLOv3
  bool IsV3() const { return version_.major == 0 && version_.minor == 2; }

  /// Check if version is YOLOv4
  bool IsV4() const { return version_.major == 0 && version_.minor == 2 &&
                              version_.is_yolo_v4; }

 private:
  DarknetVersion version_;
  size_t         weight_offset_{0};
};

}  // namespace XER::ModelsBuilder::Reader::Darknet
