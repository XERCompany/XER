#pragma once

#include <cstdlib>
#include <vector>
#include <memory>
#include <cstdint>

namespace XER::MLData::Types {

enum class DataType : uint8_t {
    FLOAT32 = 0,
    FLOAT64 = 1,
    INT32 = 2,
    INT64 = 3,
    UINT32 = 4,
    UINT8 = 5
};

class Tensor {
public:
    explicit Tensor(const std::vector<uint32_t>& shape, DataType dtype = DataType::FLOAT32);
    ~Tensor();

    Tensor(const Tensor& other);
    Tensor& operator=(const Tensor& other);
    Tensor(Tensor&& other) noexcept;
    Tensor& operator=(Tensor&& other) noexcept;
    
    std::vector<uint32_t> GetShape() const { return shape_; }
    DataType GetDataType() const { return data_type_; }
    uint64_t GetElementCount() const { return element_count_; }
    
    void* GetData() { return data_; }
    const void* GetData() const { return data_; }
    
    size_t GetMemorySize() const { return memory_size_; }
    void Zero();

private:
    bool Allocate();
    void Release();

    std::vector<uint32_t> shape_;
    DataType data_type_;
    uint64_t element_count_;
    void* data_;
    size_t memory_size_;
};

} // namespace XER::MLData::Types
