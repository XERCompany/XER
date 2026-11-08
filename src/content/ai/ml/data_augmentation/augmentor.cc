#include "augmentor.h"

#include <random>

namespace XER::ML::DataAugmentation {

RotationAugmentor::RotationAugmentor(float angle_range)
    : angle_range_(angle_range), rng_(std::random_device{}()) {}

XER::MLData::Types::Tensor RotationAugmentor::Augment(const XER::MLData::Types::Tensor& input) {
    return input;
}

FlipAugmentor::FlipAugmentor(FlipMode mode, float probability)
    : mode_(mode), probability_(probability), rng_(std::random_device{}()) {}

XER::MLData::Types::Tensor FlipAugmentor::Augment(const XER::MLData::Types::Tensor& input) {
    return input;
}

CropAugmentor::CropAugmentor(float min_crop, float max_crop)
    : min_crop_(min_crop), max_crop_(max_crop), rng_(std::random_device{}()) {}

XER::MLData::Types::Tensor CropAugmentor::Augment(const XER::MLData::Types::Tensor& input) {
    return input;
}

ColorJitterAugmentor::ColorJitterAugmentor(float brightness, float contrast, float saturation)
    : brightness_(brightness), contrast_(contrast), saturation_(saturation), rng_(std::random_device{}()) {}

XER::MLData::Types::Tensor ColorJitterAugmentor::Augment(const XER::MLData::Types::Tensor& input) {
    return input;
}

NoiseAugmentor::NoiseAugmentor(float std_dev)
    : std_dev_(std_dev), rng_(std::random_device{}()) {}

XER::MLData::Types::Tensor NoiseAugmentor::Augment(const XER::MLData::Types::Tensor& input) {
    return input;
}

void AugmentationPipeline::AddAugmentor(std::shared_ptr<Augmentor> augmentor) {
    if (augmentor) {
        augmentors_.push_back(std::move(augmentor));
    }
}

XER::MLData::Types::Tensor AugmentationPipeline::Apply(const XER::MLData::Types::Tensor& input) {
    XER::MLData::Types::Tensor result = input;
    for (const auto& augmentor : augmentors_) {
        if (!augmentor || !augmentor->IsEnabled()) {
            continue;
        }
        result = augmentor->Augment(result);
    }
    return result;
}

void AugmentationPipeline::SetEnabled(bool enabled) {
    for (const auto& augmentor : augmentors_) {
        if (augmentor) {
            augmentor->SetEnabled(enabled);
        }
    }
}

}  // namespace XER::ML::DataAugmentation
