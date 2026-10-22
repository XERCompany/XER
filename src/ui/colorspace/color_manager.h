#ifndef XER_UI_COLORSPACE_COLOR_MANAGER_H
#define XER_UI_COLORSPACE_COLOR_MANAGER_H

#include "color_space.h"
#include "hdr_manager.h"
#include <memory>

namespace XER::UI::ColorSpace {

/**
 * ColorManager - Unified color space management
 *
 * Deep module with small interface:
 * - Initialize(): Setup color management
 * - SetPreferredColorSpace(): Set desired color space
 * - GetSystemColorSpace(): Get display color space
 * - ConvertToDisplaySpace(): Convert colors for display
 *
 * All color conversion complexity hidden in implementation.
 */
class ColorManager {
public:
    static ColorManager& Instance();

    // Small interface
    bool Initialize();

    void SetPreferredColorSpace(ColorSpace space);
    ColorSpace GetPreferredColorSpace() const;

    ColorSpace GetSystemColorSpace() const;
    bool IsHDRCapable() const;

    // Color conversion
    void ConvertToDisplaySpace(
        const float* input,
        float* output,
        int pixel_count,
        ColorSpace source,
        ColorSpace target
    );

    // Transfer function application
    void ApplyTransferFunction(
        float* data,
        int pixel_count,
        TransferFunction func
    );

    // HDR management
    HDRManager& GetHDRManager();

    // Prevent copying
    ColorManager(const ColorManager&) = delete;
    ColorManager& operator=(const ColorManager&) = delete;

private:
    ColorManager();
    ~ColorManager();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::ColorSpace

#endif // XER_UI_COLORSPACE_COLOR_MANAGER_H
