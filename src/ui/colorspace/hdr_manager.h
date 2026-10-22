#ifndef XER_UI_COLORSPACE_HDR_MANAGER_H
#define XER_UI_COLORSPACE_HDR_MANAGER_H

#include "color_space.h"
#include <memory>
#include <windows.h>

namespace XER::UI::ColorSpace {

/**
 * HDRMetadata - HDR display metadata
 */
struct HDRMetadata {
    float max_luminance;      // Max nits (e.g., 1000 for HDR1000)
    float min_luminance;      // Min nits (black level)
    float max_frame_average;   // Max frame average light level (nits)
    bool supports_hdr;         // Display supports HDR
    bool supports_dolby_vision; // Supports Dolby Vision
};

/**
 * HDRManager - Manages HDR display detection and configuration
 *
 * Deep module with small interface:
 * - Initialize(): Detect HDR capabilities
 * - IsHDRCapable(): Check if display supports HDR
 * - GetHDRMetadata(): Get display HDR capabilities
 * - EnableHDR(): Enable HDR mode for window
 *
 * All Windows HDR API complexity hidden in implementation.
 */
class HDRManager {
public:
    static HDRManager& Instance();

    // Small interface
    bool Initialize();
    bool IsHDRCapable() const;
    bool IsHDREnabled() const;

    HDRMetadata GetHDRMetadata() const;
    ColorSpace GetPreferredColorSpace() const;

    bool EnableHDR(HWND hwnd);
    bool DisableHDR(HWND hwnd);

    // Color space conversion utilities
    void SetHDRParameters(
        float max_luminance,
        float min_luminance,
        float max_frame_average
    );

    // Prevent copying
    HDRManager(const HDRManager&) = delete;
    HDRManager& operator=(const HDRManager&) = delete;

private:
    HDRManager();
    ~HDRManager();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::ColorSpace

#endif // XER_UI_COLORSPACE_HDR_MANAGER_H
