// Wall-clock abstraction for the VideoEditor preview engine (Phase
// 2). The worker thread derives every playhead advance from
// NowSeconds() deltas and paces itself exclusively through
// SleepSeconds() while playing (paused waits use a condition
// variable on the real clock), so injecting a manual clock makes
// playback fully deterministic in tests — no real-time waits, exact
// per-tick playhead math. Production uses MakeSystemFrameClock().
#pragma once

#include <memory>

namespace XER::VideoEditor {

// Monotonic time source + sleep used by PreviewEngine's worker loop.
// NowSeconds must never run backwards; the epoch is arbitrary.
// SleepSeconds is the engine's ONLY wait while playing, so a manual
// implementation should advance its virtual "now" by the slept amount
// (plus a short real sleep so the worker yields the CPU) — that makes
// each tick advance the playhead by exactly the requested interval.
// Implementations are called on the engine worker thread only.
class FrameClock {
public:
    virtual ~FrameClock() = default;
    virtual double NowSeconds() const = 0;   // monotonic seconds
    virtual void SleepSeconds(double seconds) = 0;
};

// steady_clock-backed clock; SleepSeconds == sleep_for(clamped >= 0).
std::shared_ptr<FrameClock> MakeSystemFrameClock();

}  // namespace XER::VideoEditor
