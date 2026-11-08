#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugBlur : public AugBase {
public:
    explicit AugBlur(int kernel_size = 3);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Blur"; }
    void SetKernelSize(int k) { kernel_size_ = k | 1; }  // ensure odd
private:
    int kernel_size_ = 3;
};

}  // namespace XER::ML::Augmentation
