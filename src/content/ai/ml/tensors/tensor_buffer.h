#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::ML::Tensors {

/// Raw byte buffer for tensor storage (CPU-side).
/// Supports typed access to float32 data.
class TensorBuffer {
public:
    explicit TensorBuffer(size_t num_floats = 0);

    /// Resize to hold num_floats float32 values (existing data is preserved up to min size)
    void Resize(size_t num_floats);

    float*       Data()       { return reinterpret_cast<float*>(buf_.data()); }
    const float* Data() const { return reinterpret_cast<const float*>(buf_.data()); }

    size_t SizeBytes()  const { return buf_.size(); }
    size_t NumFloats()  const { return buf_.size() / sizeof(float); }

    void   Zero();
    void   Fill(float value);

    const std::vector<uint8_t>& RawBytes() const { return buf_; }

private:
    std::vector<uint8_t> buf_;
};

}  // namespace XER::ML::Tensors
