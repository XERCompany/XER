/**
 * Time constants/helpers for XER::Media TimeTicks (microseconds).
 */
#pragma once

#include <cstdint>

#include "media/base/media_types.h"

namespace XER::Media {

constexpr TimeTicks kMicrosecondsPerSecond = 1'000'000;
constexpr TimeTicks kMicrosecondsPerMillisecond = 1'000;

constexpr TimeTicks Microseconds(std::int64_t us) { return us; }
constexpr TimeTicks Milliseconds(std::int64_t ms) {
    return ms * kMicrosecondsPerMillisecond;
}
constexpr TimeTicks SecondsF(double s) {
    return static_cast<TimeTicks>(s * kMicrosecondsPerSecond);
}
constexpr double TimeTicksToSecondsF(TimeTicks t) {
    return static_cast<double>(t) / kMicrosecondsPerSecond;
}
// Converts |value| from a rational time base (num/den) to microseconds.
constexpr TimeTicks FromTimeBase(std::int64_t value, int num, int den) {
    return den != 0
               ? value * num * kMicrosecondsPerSecond / den
               : kNoTimestamp;
}

}  // namespace XER::Media
