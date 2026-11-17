// Frame-accurate timecode helpers for the VideoEditor core (monitor
// readouts, bins, the timeline ruler). UI-free, header-light: converts
// between seconds, frame counts and "HH:MM:SS:FF" display strings.
//
// Scope: NON-DROP timecode only. NTSC drop-frame (";" separator,
// skipping frame labels 00/01 on non-multiple-of-10 minutes at
// 30000/1001) is explicitly NOT supported — the frame field is a plain
// monotonically increasing frame counter modulo the nominal rate.
// Rates like 30000/1001 (29.97) still work as non-drop: frames map
// through the true rational rate while the HH:MM:SS decomposition
// counts the nominal integer rate (30), which is the standard NDF
// convention — the timecode labels frames, it does not count wall
// seconds (so "00:01:00:00" at 30000/1001 == 1800 frames == 60.06 s).
#pragma once

#include <cstdint>
#include <string>

namespace XER::VideoEditor {

// Rational frame rate: frames-per-second = num / den (30000/1001 for
// 29.97, 25/1 for PAL). Matches Project::fps_num/fps_den.
struct FrameRate {
    int num = 30;
    int den = 1;
};

// `seconds` quantized to the nearest frame, then decomposed into
// "HH:MM:SS:FF" (FF = frame within the second at the NOMINAL integer
// rate, i.e. lround(num/den)). Negative seconds clamp to
// "00:00:00:00"; non-finite input or a non-positive rate likewise
// yields the all-zero code.
std::string FormatTimecode(double seconds, int num, int den);

// Inverse of FormatTimecode. Accepts "HH:MM:SS:FF" and the hours-less
// "MM:SS:FF" (field count decides the form); every field is 1-2
// digits. Rejects — returns false — empty/malformed text, non-digit
// characters (incl. the ';' drop-frame separator), signs, whitespace,
// MM/SS >= 60 and FF >= the nominal rate. `seconds` may be null when
// only validation is wanted; on success it receives the frame-exact
// time (frames * den / num), not the nominal-rate decomposition.
bool ParseTimecode(const std::string& text, int num, int den,
                   double* seconds);

// `seconds` rounded to the nearest frame at the rational rate
// (llround: halves round away from zero). Non-positive rate or
// non-finite input returns 0; magnitudes beyond ~2^63 frames clamp.
std::int64_t SecondsToFrames(double seconds, int num, int den);

// Frame count for the same strings ParseTimecode accepts, or -1 when
// the input is rejected. Frame counts are non-negative, so -1 is an
// unambiguous failure sentinel.
std::int64_t ParseToFrames(const std::string& text, int num, int den);

// Bin/inspector duration display: "H:MM:SS.mmm" — hours unpadded,
// milliseconds rounded to nearest (carry handled, e.g. 59.9996 s ->
// "0:01:00.000"). Negative/non-finite input yields "0:00:00.000".
std::string FormatDuration(double seconds);

}  // namespace XER::VideoEditor
