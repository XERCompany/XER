#ifndef XER_UI_COLORSPACE_COLOR_SPACE_H
#define XER_UI_COLORSPACE_COLOR_SPACE_H

#include <string>

namespace XER::UI::ColorSpace {

/**
 * ColorSpace - Supported color spaces for rendering
 */
enum class ColorSpace {
    sRGB,          // Standard sRGB (no HDR)
    DisplayP3,     // Display P3 (wide gamut)
    Rec2020,       // Rec.2020 (ultra-wide gamut)
    Rec2100_PQ,    // Rec.2100 with PQ transfer (HDR)
    Rec2100_HLG,   // Rec.2100 with HLG transfer (HDR)
    ScRGB          // scRGB (HDR, linear)
};

/**
 * TransferFunction - Color transfer functions (gamma curves)
 */
enum class TransferFunction {
    sRGB,          // sRGB gamma
    PQ,            // Perceptual Quantizer (HDR PQ)
    HLG,           // Hybrid Log-Gamma (HDR HLG)
    Linear,        // Linear (no gamma)
    Gamma22,       // Gamma 2.2
    Gamma18        // Gamma 1.8
};

/**
 * ColorSpaceToString - Convert ColorSpace to string
 */
inline const char* ColorSpaceToString(ColorSpace space) {
    switch (space) {
        case ColorSpace::sRGB: return "sRGB";
        case ColorSpace::DisplayP3: return "DisplayP3";
        case ColorSpace::Rec2020: return "Rec2020";
        case ColorSpace::Rec2100_PQ: return "Rec2100_PQ";
        case ColorSpace::Rec2100_HLG: return "Rec2100_HLG";
        case ColorSpace::ScRGB: return "ScRGB";
        default: return "Unknown";
    }
}

/**
 * TransferFunctionToString - Convert TransferFunction to string
 */
inline const char* TransferFunctionToString(TransferFunction func) {
    switch (func) {
        case TransferFunction::sRGB: return "sRGB";
        case TransferFunction::PQ: return "PQ";
        case TransferFunction::HLG: return "HLG";
        case TransferFunction::Linear: return "Linear";
        case TransferFunction::Gamma22: return "Gamma2.2";
        case TransferFunction::Gamma18: return "Gamma1.8";
        default: return "Unknown";
    }
}

/**
 * IsHDR - Check if color space supports HDR
 */
inline bool IsHDR(ColorSpace space) {
    return space == ColorSpace::Rec2100_PQ ||
           space == ColorSpace::Rec2100_HLG ||
           space == ColorSpace::ScRGB;
}

/**
 * GetDefaultTransferFunction - Get default transfer for color space
 */
inline TransferFunction GetDefaultTransferFunction(ColorSpace space) {
    switch (space) {
        case ColorSpace::sRGB:
        case ColorSpace::DisplayP3:
        case ColorSpace::Rec2020:
            return TransferFunction::sRGB;
        case ColorSpace::Rec2100_PQ:
            return TransferFunction::PQ;
        case ColorSpace::Rec2100_HLG:
            return TransferFunction::HLG;
        case ColorSpace::ScRGB:
            return TransferFunction::Linear;
        default:
            return TransferFunction::sRGB;
    }
}

} // namespace XER::UI::ColorSpace

#endif // XER_UI_COLORSPACE_COLOR_SPACE_H
