#include "content/ai/sd_base/float16/sd_fp16_utils.h"

#include <cmath>

namespace XER::AI::SDBase {

uint16_t SdFp16Utils::Float32ToFp16Bits(float value) {
    if (std::isnan(value)) return 0x7E00;
    if (std::isinf(value)) return value > 0 ? 0x7C00 : 0xFC00;

    const int sign = std::signbit(value) ? 1 : 0;
    float v = std::fabs(value);
    if (v == 0.0f) return static_cast<uint16_t>(sign << 15);

    int exp = 0;
    float frac = std::frexp(v, &exp);
    exp -= 1;

    int h_exp = exp + 15;
    if (h_exp <= 0) return static_cast<uint16_t>(sign << 15);
    if (h_exp >= 31) return static_cast<uint16_t>((sign << 15) | 0x7C00);

    float mant = (frac * 2.0f - 1.0f) * 1024.0f;
    uint16_t h_mant = static_cast<uint16_t>(mant + 0.5f) & 0x03FF;
    return static_cast<uint16_t>((sign << 15) | (h_exp << 10) | h_mant);
}

float SdFp16Utils::Fp16BitsToFloat32(uint16_t bits) {
    const int sign = (bits >> 15) & 0x1;
    const int exp = (bits >> 10) & 0x1F;
    const int mant = bits & 0x03FF;

    if (exp == 0) {
        if (mant == 0) return sign ? -0.0f : 0.0f;
        const float v = std::ldexp(static_cast<float>(mant), -24);
        return sign ? -v : v;
    }
    if (exp == 31) {
        if (mant == 0) return sign ? -INFINITY : INFINITY;
        return NAN;
    }

    const float v = std::ldexp(1.0f + static_cast<float>(mant) / 1024.0f, exp - 15);
    return sign ? -v : v;
}

}  // namespace XER::AI::SDBase
