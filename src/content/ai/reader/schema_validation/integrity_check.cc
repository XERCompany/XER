#include "integrity_check.h"

#include <cstring>

namespace XER::ModelsBuilder::Reader::Schema {

bool IntegrityCheck::CheckOnnxMagic(const uint8_t* data, size_t size) {
  // ONNX protobuf files start with field tag 0x0A (field 1, wire type 2)
  // followed by the graph sub-message. Minimal check: first byte is 0x0a
  // or the file starts with any valid protobuf field tag.
  if (size < 2) return false;
  return data[0] == 0x08 || data[0] == 0x0A || data[0] == 0x12;
}

bool IntegrityCheck::CheckDarknetMagic(const uint8_t* data, size_t size) {
  // Darknet weights start with version integers; version major=0 is typical
  if (size < 12) return false;
  uint32_t major;
  std::memcpy(&major, data, 4);
  return major <= 3;  // valid major versions are 0-3
}

uint32_t IntegrityCheck::Adler32(const uint8_t* data, size_t size) {
  uint32_t a = 1, b = 0;
  constexpr uint32_t kMod = 65521;
  for (size_t i = 0; i < size; ++i) {
    a = (a + data[i]) % kMod;
    b = (b + a)       % kMod;
  }
  return (b << 16) | a;
}

bool IntegrityCheck::VerifyModel(const Core::Model& model,
                                  std::string& out_error) {
  if (model.GetLayerCount() == 0) {
    out_error = "Model has no layers";
    return false;
  }
  return true;
}

}  // namespace XER::ModelsBuilder::Reader::Schema
