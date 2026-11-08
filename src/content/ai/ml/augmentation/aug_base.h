#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::ML::Augmentation {

// Minimal image sample: flat float pixels, width, height, channels.
struct ImageSample {
    std::vector<float> pixels;   // CHW or HWC depending on convention
    int width  = 0;
    int height = 0;
    int channels = 1;
    std::string label;
};

class AugBase {
public:
    virtual ~AugBase() = default;

    // Apply augmentation in-place.  Returns false if augmentation was skipped.
    virtual bool Apply(ImageSample& sample) = 0;

    // Human-readable name of this augmentation pass.
    virtual std::string Name() const = 0;

    // Whether this augmentation is enabled.
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

protected:
    bool enabled_ = true;
};

}  // namespace XER::ML::Augmentation
