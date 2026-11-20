# 2026-11-20 — Diff Review (second bug pass: the new code itself)

Scope: `git diff 65cd81b..HEAD` (commits `93273a7`, `cf7bb61`) + dirty working
tree. Media-prep worker, JKL shuttle, transition picker, 15 core fixes, smoke
checks, CMake delay-load/staging, config.h generation, RenderStore/stream ops,
render-host worker pool, per-element compositing. Reviewed the code as it
exists now; a second agent is concurrently fixing the ui/* audit, so ui
findings may already be in flight.

Severity: **H** high / **M** medium / **L** low.

## Findings

1. **M — render_store.cc:172-181** — `BeginWrite` writes `e->offset` /
   `alloc_top` (and later `kind/width/height/payload`) while `e->seq` is still
   even; `seq` only goes odd at line 181. A racing `Read()` snapshots an even
   seq, then reads *new offset + old payload/kind* → returns a pointer into a
   fresh, unwritten extent (or a neighboring slot) as "stable". Fix: flip seq
   odd FIRST, then write offset/payload/metadata — all Entry mutations must
   live inside the odd region.

2. **M — render_stream.cc:405-415 + direct2d_renderer.cc:851-915** —
   `kDrawResource`/`kDrawVideoFrame` never check `meta.kind` or
   `meta.payload >= w*h*4`; `DrawResourcePixels`/`DrawVideoPixels` feed
   `width*height*4` bytes to `CreateBitmap`/`CopyFromMemory`. A kind-mismatched
   slot (spectrum read as BGRA) or a torn Read (#1) makes the worker read past
   the slot — possibly past the end of the shared mapping → render-process AV.
   Fix: validate `kind` per op and `payload >= w*h*4` before resolving pixels.

3. **M — tools/XERUIRender/main.cc:270** — `StreamReader::Dispatch` is not
   wrapped in try/catch; renderer calls `throw` (`RequireFrame`,
   `ThrowIfFailed`). A draw op outside a frame, a dead HWND, or any D2D failure
   kills the worker via unhandled exception → whole remote-rendering session
   lost. Fix: catch around Dispatch, drop the frame, keep the loop alive.

4. **M — media_prep.cc:256-282** — purge-generation TOCTOU: `stale` is read
   under `mutex_` (line 256-260), the lock is released, then results publish
   (`posters_.Put` at 269; `StorePeaksLocked` re-locks at 279). `PurgePath`
   interleaving between check and publish resurrects a poster/peaks entry for
   a just-removed path. Fix: run the generation check and the publish in one
   critical section (no path holds the PosterCache mutex while taking
   `mutex_`, so `mutex_ → posters_` ordering is safe), or re-check + re-remove.

5. **M — render_host.cc:157-220 + render_stream.h:204** — worker checkout is
   one-way: `in_use` is set true at `CreateRemoteRenderer` and never cleared;
   `StreamRenderer` has no destructor/release hook. Each create→destroy cycle
   leaks a ChannelEntry + 1 MB channel (shared_ptr retained) and leaves the
   worker process blocked in `Recv` until `ShutdownAll`; `ReapAndRespawn`
   skips `in_use` entries entirely → `channels_` grows unboundedly. Fix:
   release callback on renderer death (return to pool via kSetTargetHwnd 0, or
   kShutdown + erase the entry).

6. **M — widget_tree.cc:445-477** — CSS `transform` is applied via
   `SetTransform` (absolute replace), and a child's `ResetTransform` restores
   identity — not the parent's transform. Nested transformed elements render
   at the wrong offset; siblings after a transformed child lose the parent
   transform. Fix: compose transforms (multiply at the tree, or save/restore).

7. **M — widget_tree.cc:447-477** — exception unwind leaves the layer stack
   unbalanced: `widget->Render` failure rethrows before `PopLayer` /
   `ResetTransform`; a child's `RenderSubtree` throw likewise skips the pops
   (same hole exists for `PushClip`). On the stream renderer the remote
   worker's `layer_depth_` stays >0 → subsequent frames composite inside a
   stale layer. Fix: RAII guard or try/catch around the whole render+children
   span that unwinds layer/transform before rethrow.

8. **M — smoke_checks.cc:602-610** — detached watchdog `_Exit(1)` fires 15 s
   after `RunAll` starts. If checks finish at ~14.9 s and CRT teardown/FFmpeg
   cleanup runs past the deadline, the watchdog kills the process *after*
   `SMOKE RESULT OK` → spurious exit 1. Fix: make it cancellable
   (atomic-done + notify, or join before returning).

9. **M — style/widget_style_host.cc (SyncWidget/Update)** —
   `mirror_`/`widget_of_`/`last_style_` key on raw `Widget*`: a destroyed
   widget's address reused by a new widget attaches the dead widget's mirror
   (stale id/classes/invalidation history). Fix: key by stable id or erase on
   widget destruction. (ui/* — other agent may be covering.)

10. **L — media_prep.cc:144-152** — `generations_` tombstone leak:
    `PurgePath` creates an entry even with no in-flight job for the path;
    entries are only reclaimed by a later same-path `FinishJob` → grows
    unboundedly over many purges. Fix: bump only when a pending/running key
    for the path exists (queued ones were just cancelled; running ones keep
    their `pending_` entry).

11. **L — media_prep.cc:102-112 + 189-193** — `RequestImportPoster` ignores
    `Enqueue`'s result: a full queue (`max_pending`) silently drops the import
    poster and nothing retries → bin entry without poster until next run.
    Fix: surface the drop (status/catalog-dirty) or queue a retry.

12. **L — path_utils.h:131-156 `AtomicReplace`** — POSIX branch: `rename(2)`
    is atomic but NOT durable — no fdatasync on the temp file or fsync on the
    parent dir, so the "crash-safe publish" claim fails on POSIX (Windows path
    is durable via WRITE_THROUGH). Fix: fsync file + dir, or soften the doc.

13. **L — cmake/config_checks.cmake:120-125** — `configure_file` writes the
    generated `src/config.h` into the source tree: two build dirs on different
    toolchains clobber each other's header → stale `XER_HAVE_*` on incremental
    builds that skip reconfigure. Also `enable_language(OBJCXX)` mid-configure
    (Apple path) is a generator-flake risk. Fix: generate into the binary dir.

14. **L — src/ui/CMakeLists.txt:78-103** — skia runtime is a hand-maintained
    DLL list (no `libunwind`, no new transitive deps) while ggml/OCIO/OIIO use
    the new `xer_stage_runtime_dlls` objdump-walk — the skia list will rot and
    a missing dep means the exe won't start on a clean PATH. Also root
    `CMakeLists.txt:719-727`: `file(GLOB ggml-*.dll)` is configure-time —
    later-installed backends aren't picked up; `_rel` is dead code.

15. **L — ext_host.cc:238-347** — `FreeNativeModules`/`UnloadAll` FreeLibrary
    modules while the shared JS engine may still hold pointers into their
    code (registered natives captured at load) → calls into unmapped memory
    after a rescan/reload. Low (depends on whether natives register persistent
    callbacks); consider delaying frees to process exit like plugin_host.

16. **L — media_catalog.cc:1066-1074** — `std::error_code ec;
    if (ec || !exists(...))` — dead first operand (fresh `ec`); also deletes
    the peaks row on *transient* filesystem errors, not just a vanished file.

17. **L — timeline_widget.cc:400/517** — widget-side negative caches
    (`peaks_missing_`, `poster_missing_`) are never invalidated, so
    `PurgePath`/re-import can't resurrect a path the widget already wrote off
    (media_prep re-enqueues; the widget never re-asks). Fix: clear the
    missing-sets on catalog refresh / expose an invalidation hook.

18. **L — oiio_image.cc:40-69 / ocio_transform.cc:74-101** — `Ensure*Runtime`
    memoizes one `static bool`: a first-call failure (DLL absent) is permanent
    even if the runtime appears later, and the LoadLibrary probe requires ALL
    listed DLLs — `xer_collect_delayload_dlls` can glob extra stale versions
    into `XER_VE_*_RUNTIME_DLLS`, degrading the feature on one bad entry.

19. **L — plugin_host.h:77-83** — retire lists grow on every LoadAll/Unload
    cycle (deliberate, but unbounded under a hot-reload watcher). Consider
    capping or asserting a max reload count.

20. **L — media_prep.cc:43-54** — `~MediaPrep` joins workers with no
    cancellation: a hung/pathological FFmpeg decode stalls app shutdown for
    the decode's duration. Documented trade-off; consider a cancel flag +
    bounded join.

21. **L — render_store.cc:35-48 (Create)** — `CreateFileMapping` on an
    existing name returns the same mapping (`ERROR_ALREADY_EXISTS` unchecked):
    a second `Create` memsets the live header → state stomp. POSIX: destructor
    never `shm_unlink`s → segment leak (Windows is the target; note only).

22. **Nit** — `media_smoke_ffmpeg.err` (2-line log) committed at repo root;
    remove + gitignore. `smoke_checks.cc:531` trailing whitespace.

## Verified OK (checked, no defect)

- `media_prep` member order (`video_editor_app.h:211`): declared after
  `catalog_` → dtor joins workers before the catalog dies; `mutex_` and
  `db_mutex_` are never held across each other (no lock-order inversion).
- JKL ladder (`video_editor_app.cc:1764-1823`): ±{1,2,4,8} cap, opposite-key
  step-down + flip at 1x, K/Space/pause-at-end all reset `shuttle_rate_`;
  engine `SetRate` rebases the anchor without a playhead jump.
- `AddTransition` (timeline_model.cc:937-981): finite/positive duration,
  adjacency, dup, track-kind, lock checks — picker boundaries covered.
- `MacroCommand::Apply` rollback loop (timeline_history.cc:1000-1012):
  reverts `[0,i)` correctly; Redo re-queues the failed command.
- `plugin_host` param-table validation: null/oversized/null-name guarded.
- `audio_pcm` drain: rate/channel mid-stream change is fatal, payload bounds
  checked before reads, conversion errors latch.
- `Enqueue` dedup/back-pressure, `WaitIdle` predicate, `Consume*` drains,
  `CancelQueued`/`CancelAllQueued` — consistent under mutex.

## Missing test coverage (critical invariants)

- **JKL ladder**: `ShuttleStep` rate progression/direction flip/pause-at-end
  — untested (no harness seam; consider extracting the pure ladder step).
- **Purge-during-poster**: test 7 covers peaks via the safe path; the #4
  publish-after-purge window is a race the current seam can't hit — a
  `FinishJob` hook or repeated-run stress case would help.
- **RenderStore seqlock tear** (`tests/ui/render_store_tests.cc` covers API,
  not concurrent BeginWrite/Read torn metadata — #1) and the `payload >=
  w*h*4` guard (#2).
- **Transition pair insertion**: `AddTransition` model rules have no direct
  tests in this diff (adjacency/dup/duration>0/locked track).
- `ExtractPoster` rowid-reuse after Remove+reimport (stale `poster_path` can
  land on a recycled id row) — untested edge.
