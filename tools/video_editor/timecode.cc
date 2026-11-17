// Timecode implementation — see timecode.h for the non-drop contract.
#include "timecode.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace XER::VideoEditor {
namespace {

// Nominal integer rate the HH:MM:SS decomposition and the FF bound
// count against: 30 for 30000/1001, 25 for 25/1. 0 marks an invalid
// rate so every public entry point can bail uniformly.
int NominalFps(int num, int den) {
    if (num <= 0 || den <= 0) return 0;
    const int fps = static_cast<int>(std::lround(
        static_cast<double>(num) / static_cast<double>(den)));
    return fps > 0 ? fps : 0;
}

// Split "a:b:c(:d)" into fields; each must be 1-2 digits of nothing
// but ASCII digits — which also rejects signs, whitespace, the ';'
// drop-frame separator and empty fields. Returns the field count, or
// 0 on malformed input.
int SplitTimecodeFields(const std::string& text,
                        int* fields /*size >= 4*/) {
    int count = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        if (count >= 4) return 0;   // more than 4 fields
        const std::size_t colon = text.find(':', pos);
        const std::size_t end =
            colon == std::string::npos ? text.size() : colon;
        const std::size_t len = end - pos;
        if (len == 0 || len > 2) return 0;
        int value = 0;
        for (std::size_t i = pos; i < end; ++i) {
            const char c = text[i];
            if (c < '0' || c > '9') return 0;
            value = value * 10 + (c - '0');
        }
        fields[count++] = value;
        if (colon == std::string::npos) break;
        pos = colon + 1;
    }
    return count;
}

}  // namespace

std::string FormatTimecode(double seconds, int num, int den) {
    const int fps = NominalFps(num, den);
    if (fps <= 0 || !std::isfinite(seconds)) return "00:00:00:00";
    std::int64_t frames = SecondsToFrames(seconds, num, den);
    if (frames < 0) frames = 0;
    const std::int64_t ff = frames % fps;
    const std::int64_t total = frames / fps;
    const std::int64_t hh = total / 3600;
    const std::int64_t mm = (total / 60) % 60;
    const std::int64_t ss = total % 60;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld:%02lld",
                  static_cast<long long>(hh), static_cast<long long>(mm),
                  static_cast<long long>(ss), static_cast<long long>(ff));
    return buf;
}

bool ParseTimecode(const std::string& text, int num, int den,
                   double* seconds) {
    const std::int64_t frames = ParseToFrames(text, num, den);
    if (frames < 0) return false;
    if (seconds != nullptr) {
        *seconds = static_cast<double>(frames) *
                   static_cast<double>(den) / static_cast<double>(num);
    }
    return true;
}

std::int64_t SecondsToFrames(double seconds, int num, int den) {
    if (num <= 0 || den <= 0 || !std::isfinite(seconds)) return 0;
    const double frames =
        seconds * static_cast<double>(num) / static_cast<double>(den);
    // llround is unspecified outside the representable range; clamp.
    constexpr double kMax = 9.0e18;
    if (frames >= kMax) return INT64_MAX;
    if (frames <= -kMax) return INT64_MIN;
    return std::llround(frames);
}

std::int64_t ParseToFrames(const std::string& text, int num, int den) {
    const int fps = NominalFps(num, den);
    if (fps <= 0) return -1;
    int fields[4] = {0, 0, 0, 0};
    const int count = SplitTimecodeFields(text, fields);
    // 3 fields: MM:SS:FF; 4 fields: HH:MM:SS:FF. Other shapes are
    // malformed ("SS:FF", "H:M:S:F:x", bare seconds, ...).
    if (count != 3 && count != 4) return -1;
    const int hh = count == 4 ? fields[0] : 0;
    const int mm = fields[count - 3];
    const int ss = fields[count - 2];
    const int ff = fields[count - 1];
    if (mm >= 60 || ss >= 60 || ff >= fps) return -1;
    return (static_cast<std::int64_t>(hh) * 60 + mm) * 60 * fps +
           static_cast<std::int64_t>(ss) * fps + ff;
}

std::string FormatDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0) return "0:00:00.000";
    const double ms = seconds * 1000.0;
    // ~106751991167 hours is int64 ms overflow territory; clamp well
    // before that — bin rows never show millennia.
    const std::int64_t total_ms =
        ms >= 9.0e15 ? std::llround(9.0e15) : std::llround(ms);
    const std::int64_t hh = total_ms / 3600000;
    const std::int64_t mm = (total_ms / 60000) % 60;
    const std::int64_t ss = (total_ms / 1000) % 60;
    const std::int64_t mmm = total_ms % 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%03lld",
                  static_cast<long long>(hh), static_cast<long long>(mm),
                  static_cast<long long>(ss),
                  static_cast<long long>(mmm));
    return buf;
}

}  // namespace XER::VideoEditor
