#include "darknet_weights_loader.h"

#include <cstring>
#include <fstream>

namespace XER::ModelsBuilder::Reader::Darknet {

bool DarknetWeightsLoader::LoadFile(const std::string& filepath) {
  std::ifstream file(filepath, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  data_.resize(static_cast<size_t>(size));
  if (!file.read(reinterpret_cast<char*>(data_.data()), size)) return false;

  return LoadBuffer(data_.data(), data_.size());
}

bool DarknetWeightsLoader::LoadBuffer(const uint8_t* data, size_t size) {
  if (!version_checker_.Parse(data, size)) return false;

  if (data_.empty() || data_.data() != data) {
    data_.assign(data, data + size);
  }

  cursor_ = version_checker_.GetWeightOffset();
  return true;
}

std::vector<float> DarknetWeightsLoader::ReadFloats(size_t count) {
  size_t bytes_needed = count * sizeof(float);
  if (cursor_ + bytes_needed > data_.size()) return {};

  std::vector<float> out(count);
  std::memcpy(out.data(), data_.data() + cursor_, bytes_needed);
  cursor_ += bytes_needed;
  return out;
}

size_t DarknetWeightsLoader::GetWeightBytes() const {
  size_t offset = version_checker_.GetWeightOffset();
  if (data_.size() <= offset) return 0;
  return data_.size() - offset;
}

}  // namespace XER::ModelsBuilder::Reader::Darknet
