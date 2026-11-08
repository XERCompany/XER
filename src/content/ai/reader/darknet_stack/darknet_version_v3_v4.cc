#include "darknet_version_v3_v4.h"

#include <cstring>
#include <sstream>

namespace XER::ModelsBuilder::Reader::Darknet {

bool DarknetVersionV3V4::Parse(const uint8_t* data, size_t size) {
  // Darknet .weights header layout (little-endian):
  //   major   : int32 (4 bytes)
  //   minor   : int32 (4 bytes)
  //   revision: int32 (4 bytes)
  //   seen    : int64 (8 bytes) if (major*10+minor) >= 2, else int32
  // Total header: 20 or 16 bytes
  if (size < 12) return false;

  const uint8_t* p = data;
  auto read32 = [&](uint32_t& out) {
    std::memcpy(&out, p, 4); p += 4;
  };

  read32(version_.major);
  read32(version_.minor);
  read32(version_.revision);

  int version_code = static_cast<int>(version_.major * 10 + version_.minor);
  if (version_code >= 2 && size >= 20) {
    std::memcpy(&version_.seen, p, 8);
    p += 8;
    weight_offset_ = 20;
  } else {
    uint32_t seen32 = 0;
    if (size >= 16) { std::memcpy(&seen32, p, 4); p += 4; }
    version_.seen = seen32;
    weight_offset_ = 16;
  }

  // Heuristic: YOLOv4 uses minor==2 with revision >= 67
  version_.is_yolo_v4 = (version_.minor == 2 && version_.revision >= 67) ? 1 : 0;

  return true;
}

std::string DarknetVersionV3V4::GetVersionString() const {
  std::ostringstream oss;
  oss << "Darknet weights v" << version_.major << "." << version_.minor
      << "." << version_.revision << " (seen=" << version_.seen << ")";
  if (version_.is_yolo_v4) oss << " [YOLOv4+]";
  return oss.str();
}

}  // namespace XER::ModelsBuilder::Reader::Darknet
