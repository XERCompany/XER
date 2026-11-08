#include "validator.h"

namespace XER::MLData::Validation {

DataValidator& DataValidator::GetInstance() {
    static DataValidator instance;
    return instance;
}

ValidationResult DataValidator::Validate(const Types::Dataset& dataset) {
    if (dataset.GetBatchCount() == 0) {
        return {false, "Dataset is empty", 0.0f};
    }
    // Check that all batches are non-null and have at least one sample.
    size_t valid_batches = 0;
    for (size_t b = 0; b < dataset.GetBatchCount(); ++b) {
        auto batch = dataset.GetBatch(b);
        if (batch && batch->GetBatchSize() > 0) {
            ++valid_batches;
        }
    }
    float completeness = static_cast<float>(valid_batches) /
                         static_cast<float>(dataset.GetBatchCount());
    if (completeness < 1.0f) {
        return {false, "Some batches are empty or null",
                completeness};
    }
    return {true, "", 1.0f};
}

ValidationResult DataValidator::ValidateBatch(const Types::Batch& batch) {
    if (batch.GetBatchSize() == 0) {
        return {false, "Batch is empty", 0.0f};
    }
    size_t valid = 0;
    for (size_t s = 0; s < batch.GetBatchSize(); ++s) {
        auto tensor = batch.GetSample(s);
        if (tensor && tensor->GetData() && tensor->GetElementCount() > 0) {
            ++valid;
        }
    }
    float completeness = static_cast<float>(valid) /
                         static_cast<float>(batch.GetBatchSize());
    if (completeness < 1.0f) {
        return {false, "Some samples have no data", completeness};
    }
    return {true, "", 1.0f};
}

ValidationResult DataValidator::ValidateTensor(const Types::Tensor& tensor) {
    if (!tensor.GetData()) {
        return {false, "Tensor has no data", 0.0f};
    }
    if (tensor.GetElementCount() == 0) {
        return {false, "Tensor has zero elements", 0.0f};
    }
    if (tensor.GetShape().empty()) {
        return {false, "Tensor has no shape", 0.0f};
    }
    return {true, "", 1.0f};
}

} // namespace XER::MLData::Validation
