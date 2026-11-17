# Phase 1A — Engine Foundation: COMPLETE (2026-11-15)

Executed via subagent-driven development (fresh implementer + task
reviewer per task, scoped re-reviews, final whole-branch review).
Branch `feature/xer-studio-phase1`, commits `6742124..a0f5058`.
**MERGED to main 2026-11-15** (fast-forward; branch pointer kept).

Plan: `docs/superpowers/plans/2026-11-15-phase1a-engine-foundation.md`
Ledger/reports (deleted post-completion per SDD): were under
`.superpowers/sdd/2026-11-15-phase1a-engine-foundation/`.

## What landed

| Task | Commit | Result |
|------|--------|--------|
| 1. XER_Video_Smoke | `33c9c65` | orphan lib now has consumer; review clean |
| 2. audio-only VideoDecoder | `4d1cecd`+`1e2bd63` | 1 fix round (reuse-state leak, IsOpen-after-fail, tap starvation); review clean |
| 3. encoder internals refactor | `28564a5` | pure refactor, header untouched; review clean |
| 4. EncodeAudioVideoStream mux | `a0f5058` | two-stream mp4 mux + 4 tests; review clean |
| 5. verification | — | ctest 4/4, XER_UI_Tests 64/64, build clean |

Final whole-branch review verdict: **Ready to merge — Yes.** No
Critical/Important in-diff findings; cross-task consumer safety verified
(preview path can't reach relaxed decoder — `ProbeVideo` still gates).

## Rulings made during execution (for audit)

- Branch-in-place `feature/xer-studio-phase1` over git worktree — warm
  Ninja tree worth more than isolation; tree was clean. Cost if wrong:
  user's uncommitted work could mix (none existed).
- Task-2 ⚠️ (no zero-stream-container test): deferred — 3-line branch
  verified by inspection; fixture synthesis cost > coverage value.
- Task-5 `docs/`+`.superpowers/` left untracked — user decision pending.
- Task-5 verification-only: no diff → no task review (evidence = report).

## Handoff items for next phases (from final review)

- **Before 1C**: `av_rescale_q` fix for `VideoDecoder::Seek` fallback
  timestamp units (audio-only seeks land wrong); decide audio-only
  progress source (`position_seconds` never advances — use
  `AudioFrameInfo.pts`); AAC-priming/`GetAudioStartSeconds` at mux;
  atomic temp→commit for partial mux files.
- **Before/with 1B-1D**: fix Skia `BlendSrcOver` premultiplied bug
  (breaks Alpha transitions); relax `ProbeVideo` for audio-only import;
  opportunistic `#ifdef _WIN32` on `EnableAudioTap`.
- Full details: `architecture/video-pipeline-facts.md` → "Phase-1A
  additions".

## Process notes (reusable)

- `out/vcenv.bat <cmd>` wraps vcvars64 — required for ALL cmake/ctest
  invocations (see build-and-test-infra.md).
- SDD scripts live in `~/.agents/skills/subagent-driven-development/scripts/`
  (`sdd-workspace`, `task-brief`, `review-package`) — run from repo root.
- Test filter gotcha: `--filter` is substring match on test names;
  `mux`/`audio_only` don't select all intended tests — use suite names.
