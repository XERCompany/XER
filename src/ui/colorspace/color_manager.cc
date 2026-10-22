#include "color_manager.h"
#include <algorithm>
#include <cmath>

namespace XER::UI::ColorSpace {

// Private implementation
class ColorManager::Impl {
public:
    Impl()
        : initialized_(false)
        , preferred_color_space_(ColorSpace::sRGB)
        , system_color_space_(ColorSpace::sRGB)
        , hdr_manager_(HDRManager::Instance()) {}

    bool Initialize() {
        if (initialized_) {
            return true;
        }

        // Initialize HDR manager
        hdr_manager_.Initialize();

        // Detect system color space
        // In a real implementation, this would query the display
        system_color_space_ = DetectSystemColorSpace();

        // Set preferred based on capabilities
        if (hdr_manager_.IsHDRCapable()) {
            preferred_color_space_ = ColorSpace::Rec2100_PQ;
        }

        initialized_ = true;
        return true;
    }

    void SetPreferredColorSpace(ColorSpace space) {
        preferred_color_space_ = space;
    }

    ColorSpace GetPreferredColorSpace() const {
        return preferred_color_space_;
    }

    ColorSpace GetSystemColorSpace() const {
        return system_color_space_;
    }

    bool IsHDRCapable() const {
        return hdr_manager_.IsHDRCapable();
    }

    void ConvertToDisplaySpace(
        const float* input,
        float* output,
        int pixel_count,
        ColorSpace source,
        ColorSpace target
    ) {
        if (source == target) {
            // No conversion needed, just copy
            std::copy(input, input + pixel_count * 3, output);
            return;
        }

        // Convert source to linear
        std::vector<float> linear_buffer(pixel_count * 3);
        ConvertToLinear(input, linear_buffer.data(), pixel_count, source);

        // Convert linear to target
        ConvertFromLinear(linear_buffer.data(), output, pixel_count, target);
    }

    void ApplyTransferFunction(
        float* data,
        int pixel_count,
        TransferFunction func
    ) {
        for (int i = 0; i < pixel_count * 3; ++i) {
            data[i] = ApplyTransfer(data[i], func);
        }
    }

    HDRManager& GetHDRManager() {
        return hdr_manager_;
    }

private:
    ColorSpace DetectSystemColorSpace() {
        // In a real implementation, this would:
        // 1. Query Windows display settings
        // 2. Check DXGI color space
        // 3. Query monitor EDID for color gamut

        // For now, assume sRGB
        return ColorSpace::sRGB;
    }

    void ConvertToLinear(
        const float* input,
        float* output,
        int pixel_count,
        ColorSpace space
    ) {
        TransferFunction func = GetDefaultTransferFunction(space);
        for (int i = 0; i < pixel_count * 3; ++i) {
            output[i] = ApplyInverseTransfer(input[i], func);
        }
    }

    void ConvertFromLinear(
        const float* input,
        float* output,
        int pixel_count,
        ColorSpace space
    ) {
        TransferFunction func = GetDefaultTransferFunction(space);
        for (int i = 0; i < pixel_count * 3; ++i) {
            output[i] = ApplyTransfer(input[i], func);
        }
    }

    float ApplyTransfer(float value, TransferFunction func) {
        // Clamp to [0, 1]
        value = std::max(0.0f, std::min(1.0f, value));

        switch (func) {
            case TransferFunction::sRGB:
                // sRGB gamma
                if (value <= 0.04045f) {
                    return value / 12.92f;
                }
                return std::pow((value + 0.055f) / 1.055f, 2.4f);

            case TransferFunction::PQ:
                // PQ transfer (simplified)
                // Full implementation would use ST 2084 specification
                return std::pow(value, 1.0f / 2.4f);  // Simplified

            case TransferFunction::HLG:
                // HLG transfer (simplified)
                // Full implementation would use Rec.2100 HLG specification
                if (value <= 0.5f) {
                    return std::pow(2.0f * value, 2.0f) * 3.0f;
                }
                return (std::exp(12.0f * (value - 1.0f)) + 1.0f) * 3.0f;

            case TransferFunction::Linear:
                return value;

            case TransferFunction::Gamma22:
                return std::pow(value, 2.2f);

            case TransferFunction::Gamma18:
                return std::pow(value, 1.8f);

            default:
                return value;
        }
    }

    float ApplyInverseTransfer(float value, TransferFunction func) {
        // Clamp to [0, 1]
        value = std::max(0.0f, std::min(1.0f, value));

        switch (func) {
            case TransferFunction::sRGB:
                // Inverse sRGB gamma
                if (value <= 0.0031308f) {
                    return value * 12.92f;
                }
                return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;

            case TransferFunction::PQ:
                // Inverse PQ (simplified)
                return std::pow(value, 2.4f);

            case TransferFunction::HLG:
                // Inverse HLG (simplified)
                if (value <= 1.0f) {
                    return std::sqrt(value / 3.0f) / 2.0f;
                }
                return std::log(value / 3.0f - 1.0f) / 12.0f + 1.0f;

            case TransferFunction::Linear:
                return value;

            case TransferFunction::Gamma22:
                return std::pow(value, 1.0f / 2.2f);

            case TransferFunction::Gamma18:
                return std::pow(value, 1.0f / 1.8f);

            default:
                return value;
        }
    }

    bool initialized_;
    ColorSpace preferred_color_space_;
    ColorSpace system_color_space_;
    HDRManager& hdr_manager_;
};

// ColorManager implementation
ColorManager::ColorManager() : impl_(std::make_unique<Impl>()) {}

ColorManager::~ColorManager() = default;

ColorManager& ColorManager::Instance() {
    static ColorManager instance;
    return instance;
}

bool ColorManager::Initialize() {
    return impl_->Initialize();
}

void ColorManager::SetPreferredColorSpace(ColorSpace space) {
    impl_->SetPreferredColorSpace(space);
}

ColorSpace ColorManager::GetPreferredColorSpace() const {
    return impl_->GetPreferredColorSpace();
}

ColorSpace ColorManager::GetSystemColorSpace() const {
    return impl_->GetSystemColorSpace();
}

bool ColorManager::IsHDRCapable() const {
    return impl_->IsHDRCapable();
}

void ColorManager::ConvertToDisplaySpace(
    const float* input,
    float* output,
    int pixel_count,
    ColorSpace source,
    ColorSpace target
) {
    impl_->ConvertToDisplaySpace(input, output, pixel_count, source, target);
}

void ColorManager::ApplyTransferFunction(
    float* data,
    int pixel_count,
    TransferFunction func
) {
    impl_->ApplyTransferFunction(data, pixel_count, func);
}

HDRManager& ColorManager::GetHDRManager() {
    return impl_->GetHDRManager();
}

} // namespace XER::UI::ColorSpace
