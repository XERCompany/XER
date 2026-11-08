#include "tensor_buffer.h"
#include <cstring>

namespace XER::ML::Tensors {

TensorBuffer::TensorBuffer(size_t num_floats)
    : buf_(num_floats * sizeof(float), 0) {}

void TensorBuffer::Resize(size_t num_floats) {
    buf_.resize(num_floats * sizeof(float), 0);
}

void TensorBuffer::Zero() {
    std::memset(buf_.data(), 0, buf_.size());
}

void TensorBuffer::Fill(float value) {
    float* p = Data();
    const size_t n = NumFloats();
    for (size_t i = 0; i < n; ++i) p[i] = value;
}

}  // namespace XER::ML::Tensors
