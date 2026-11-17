# Phase 1B — Timeline Core: COMPLETE (2026-11-15)

Executed via subagent-driven development (fresh implementer + task
reviewer per task, scoped re-reviews, final whole-branch review +
single fix wave). Branch `feature/xer-studio-1b-timeline`, commits
`a0f5058..b966af9`.
**MERGED to main 2026-11-15** (fast-forward; branch pointer kept).

Plan: `docs/superpowers/plans/2026-11-15-phase1b-timeline-core.md`

## What landed

| Task | Commits | Result |
|------|---------|--------|
| 1. model + lib + harness | `e422789` | review clean |
| 2. timeline_history | `a4db44c`+`f2c1df1` | 1 fix round (merged-redo transition resurrection) |
| 3. project_store JSON | `a6798fd`+`c346606` | 1 fix round (load overlap check + duration drop) |
| 4. clip_poster | `32b5c6e` | review clean |
| 5. media_catalog + ProbeVideo | `5489535` | review clean (3 brief deviations proven correct) |
| 6. verification | — | ctest 5/5, all suites pass |
| Final fix wave | `b966af9` | 4 findings: NaN guards, Load try/catch, session loop guard, history-reset docs |

Final whole-branch review verdict: **With fixes** → all 4 fixed and
re-verified ADDRESSED, no new breakage.

## Rulings made during execution

- ProbeVideo relaxation accepted — header contract already promised
  audio-only; VideoSession degradation documented (later hardened by
  the loop guard in fix wave).
- Task-2 plan-mandated merge-divergence: FIXED (decline merge when
  transitions changed) rather than deferred — 1D drag gestures would
  hit it constantly.
- Task-3 plan-mandated missing overlap check: FIXED — .xerproj is
  hand-editable by design; invariant must be airtight pre-1C.
- Task-5 brief deviations accepted: ill-formed `const&` signature,
  Dummy-struct fix that wouldn't compile, empty-HOME guard — all were
  brief bugs, implementations verified correct.
- Task-6 verification-only: no task review (empty diff).

## Handoff items for 1C/1D

- **1C**: Skia `BlendSrcOver` premultiply bug (breaks Alpha
  transitions); `VideoDecoder::Seek` fallback `av_rescale_q`; audio-only
  `position_seconds` → use `AudioFrameInfo.pts`; atomic temp→commit for
  mux partial files; AAC priming/`GetAudioStartSeconds`.
- **1C/1D**: model has NO mutators for track properties
  (name/muted/hidden/gain_db) or project settings (canvas/fps/bg) —
  inspector edits need new model ops + history commands.
- **1D**: `history.Clear()` after every project load (contract now
  documented); audio-only preview path = Bridge decoder, not
  VideoSession; consider `History::IsMacroActive()`/RAII macro pairing.
- **1E**: `video_editor --info`/`--export` CLI; wiki claims update.

## Process notes

- `task-brief` script breaks on unbalanced ``` fences in plans —
  1B plan had one at ~L1569; briefs were hand-extracted via sed.
  Check fence balance (`grep -c '^```'` should be even) when writing
  future plans.
- `vcenv.bat` mangles RELATIVE exe paths — use absolute paths when
  running test exes directly.
- `--filter` on XER_UI_Tests is substring match — `mux`/`audio_only`
  don't select all intended tests; prefer suite-level names.
