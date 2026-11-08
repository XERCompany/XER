#pragma once
#include "aug_base.h"

namespace XER::ML::Augmentation {

class AugFlip : public AugBase {
public:
    enum class Direction { Horizontal, Vertical, Both };
    explicit AugFlip(Direction dir = Direction::Horizontal, float prob = 0.5f);
    bool Apply(ImageSample& sample) override;
    std::string Name() const override { return "Flip"; }
private:
    Direction dir_;
    float prob_;
};

}  // namespace XER::ML::Augmentation
