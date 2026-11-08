#pragma once

#include "../data_types/tensor.h"
#include <memory>
#include <random>
#include <vector>
#include <string>

namespace XER::ML::DataAugmentation {

/// @brief Base class for image augmentation
/// 
/// Abstract interface for data augmentation operations using OpenCV.
/// Implementations provide specific transformations (rotation, flip, crop, etc.)
/// for training-time data variation to improve model generalization.
class Augmentor {
public:
    virtual ~Augmentor() = default;
    
    /// Apply augmentation transformation to input tensor
    /// @param input Input tensor (image in HxWxC format, float values [0,1])
    /// @return Augmented tensor with same shape as input
    virtual XER::MLData::Types::Tensor Augment(const XER::MLData::Types::Tensor& input) = 0;
    
    /// Get human-readable augmentation name
    /// @return Augmentor type identifier
    virtual std::string GetAugmentationName() const = 0;
    
    /// Enable/disable augmentation (for train vs eval modes)
    virtual void SetEnabled(bool enabled) { enabled_ = enabled; }
    
    bool IsEnabled() const { return enabled_; }

protected:
    bool enabled_ = true;
};

/// @brief Random rotation augmentor using OpenCV
/// 
/// Applies random rotation in range [-angle_range, angle_range]
/// Uses cv::warpAffine for efficient GPU-accelerated transformation
class RotationAugmentor : public Augmentor {
public:
    /// Create rotation augmentor
    /// @param angle_range Max rotation angle in degrees (±angle_range)
    explicit RotationAugmentor(float angle_range = 15.0f);
    
    /// Apply random rotation
    XER::MLData::Types::Tensor Augment(const XER::MLData::Types::Tensor& input) override;
    std::string GetAugmentationName() const override { return "Rotation"; }
    
    void SetAngleRange(float angle_range) { angle_range_ = angle_range; }

private:
    float angle_range_;
    std::mt19937 rng_;
};

/// @brief Random flip augmentor
/// 
/// Randomly flips image horizontally and/or vertically using OpenCV
class FlipAugmentor : public Augmentor {
public:
    enum class FlipMode : unsigned char {
        Horizontal = 0,
        Vertical = 1,
        Both = 2
    };
    
    /// Create flip augmentor
    /// @param mode Flip orientation (horizontal, vertical, or both)
    /// @param probability Probability of flipping [0, 1]
    FlipAugmentor(FlipMode mode = FlipMode::Horizontal, float probability = 0.5f);
    
    /// Apply random flip
    XER::MLData::Types::Tensor Augment(const XER::MLData::Types::Tensor& input) override;
    std::string GetAugmentationName() const override { return "Flip"; }

private:
    FlipMode mode_;
    float probability_;
    std::mt19937 rng_;
};

/// @brief Random crop/zoom augmentor
/// 
/// Extracts random region and resizes to original dimensions
/// Creates implicit zoom/translation augmentation
class CropAugmentor : public Augmentor {
public:
    /// Create crop augmentor
    /// @param min_crop Minimum crop ratio [0, 1] (0.8 = 80% of image)
    /// @param max_crop Maximum crop ratio [0, 1] (1.0 = no crop)
    CropAugmentor(float min_crop = 0.8f, float max_crop = 1.0f);
    
    /// Apply random crop
    XER::MLData::Types::Tensor Augment(const XER::MLData::Types::Tensor& input) override;
    std::string GetAugmentationName() const override { return "Crop"; }

private:
    float min_crop_;
    float max_crop_;
    std::mt19937 rng_;
};

/// @brief Color augmentor (brightness, contrast, saturation)
/// 
/// Randomly adjusts brightness, contrast, and color saturation
/// Helps model learn color-invariant features
class ColorJitterAugmentor : public Augmentor {
public:
    /// Create color jitter augmentor
    /// @param brightness Brightness factor range (0.2 = ±20%)
    /// @param contrast Contrast factor range (0.2 = ±20%)
    /// @param saturation Saturation factor range (0.2 = ±20%)
    ColorJitterAugmentor(float brightness = 0.2f,
                         float contrast = 0.2f,
                         float saturation = 0.2f);
    
    /// Apply random color jitter
    XER::MLData::Types::Tensor Augment(const XER::MLData::Types::Tensor& input) override;
    std::string GetAugmentationName() const override { return "ColorJitter"; }

private:
    float brightness_;
    float contrast_;
    float saturation_;
    std::mt19937 rng_;
};

/// @brief Gaussian noise augmentor
/// 
/// Adds random Gaussian noise to simulate sensor noise
/// Improves robustness to noisy inputs
class NoiseAugmentor : public Augmentor {
public:
    /// Create noise augmentor
    /// @param std_dev Standard deviation of Gaussian noise (0 to 1 scale)
    explicit NoiseAugmentor(float std_dev = 0.05f);
    
    /// Apply random Gaussian noise
    XER::MLData::Types::Tensor Augment(const XER::MLData::Types::Tensor& input) override;
    std::string GetAugmentationName() const override { return "GaussianNoise"; }

private:
    float std_dev_;
    std::mt19937 rng_;
};

/// @brief Augmentation pipeline
/// 
/// Chains multiple augmentors and applies them sequentially
/// Configuration can vary between training (augmentation enabled)
/// and evaluation (augmentation disabled)
class AugmentationPipeline {
public:
    /// Create empty pipeline
    AugmentationPipeline() = default;
    
    /// Add augmentor to end of pipeline
    /// @param augmentor Augmentor to add
    void AddAugmentor(std::shared_ptr<Augmentor> augmentor);
    
    /// Remove all augmentors
    void Clear() { augmentors_.clear(); }
    
    /// Apply all augmentations in sequence
    /// @param input Input tensor
    /// @return Result after applying all augmentors
    XER::MLData::Types::Tensor Apply(const XER::MLData::Types::Tensor& input);
    
    /// Enable/disable all augmentors (for train/eval modes)
    /// @param enabled true for training, false for inference
    void SetEnabled(bool enabled);
    
    /// Get number of augmentors in pipeline
    size_t GetAugmentorCount() const { return augmentors_.size(); }

private:
    std::vector<std::shared_ptr<Augmentor>> augmentors_;
};

} // namespace XER::ML::DataAugmentation
