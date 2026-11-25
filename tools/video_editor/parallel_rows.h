// Row-parallel pixel-pass helper for the VideoEditor render path
// (chromium render-port follow-up — the compositor's per-pixel loops
// are row-independent gathers/writes, so they split cleanly across a
// shared worker pool; see
// docs/wiki/architecture/chromium-render-port-plan.md).
//
// Model: VeParallelFor runs fn(unit_begin, unit_end) over [0, units)
// split into contiguous ranges executed on a persistent pool (the
// CALLER also participates and blocks until every range finishes —
// no async fire-and-forget, no per-call thread spawns). Ranges are
// disjoint and each unit's math is a pure function of its index, so
// the output is byte-identical to a serial pass — determinism the
// EffectFrameCache keys rely on.
//
// Tuning: XER_VE_THREADS=N selects the parts a pass splits into
// (N <= 1 -> serial; unset/invalid -> min(hardware_concurrency, 8)).
// The env is re-read per call so a smoke check can A/B thread counts
// in-process. Passes smaller than kVeParallelMinUnits stay serial —
// pool dispatch overhead isn't worth it below ~64 rows of work.
#pragma once

#include <functional>

namespace XER::VideoEditor {

// Units (rows/cells) below which a pass runs inline on the caller.
// 1080p/4K frames always parallelize; tiny test/still frames don't.
constexpr int kVeParallelMinUnits = 64;

// Resolved worker count for row-parallel passes: XER_VE_THREADS=N,
// N <= 1 -> serial. Unset or unparseable ->
// min(hardware_concurrency, 8). Clamped to the pool size.
int VeRenderThreadCount();

// Runs fn(begin, end) over [0, units). `fn` must write only the
// output units inside its own [begin, end) range and be safe to run
// concurrently across ranges. Blocks until all ranges complete.
void VeParallelFor(int units,
                   const std::function<void(int b, int e)>& fn);

// Row convenience — VeParallelFor over [0, rows) for y-indexed
// per-pixel loops.
inline void VeParallelRows(
    int rows, const std::function<void(int y0, int y1)>& fn) {
    VeParallelFor(rows, fn);
}

}  // namespace XER::VideoEditor
