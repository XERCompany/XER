# Video Editor Core/Infra Bug Audit (Part 2 of 2)

- **Scope**: `tools/video_editor/` excluding `ui/` — compositor, export engine,
  audio mixer, media catalog/prep, poster cache, project store/autosave, media
  decoder, generated sources, bitmap font, timecode, presets, timeline/history,
  keyboard map, plugin host, `playback/`, `io/`, `plugins/`, `ai/`,
  `extensions/`, `lab/`, `color/`.
- **Method**: source inspection; findings confirmed against call sites.
- **Severity order**: CRASH → DATA-LOSS → LOGIC → LEAK → STUB.

## Findings

### CRASH

1. **CRASH** — `tools/video_editor/plugin_host.cc:141-145` — `LoadAll` dereferences
   `params[p]` for every `p < n` without validating the plugin-reported pointer:
   a malformed plugin can return `params == nullptr` (or a dangling/short
   buffer) with `n > 0` → immediate null/OOB dereference during plugin
   discovery. *Fix*: after `params_fn(i, &n)` require `params != nullptr` when
   `n > 0`, bound `n` to a sane max, and validate each descriptor
   (`param->name != nullptr`, param_count < 64) before use; reject the plugin on
   failure.

2. **CRASH** — `tools/video_editor/plugin_host.cc:88-91, 167-171` — `Unload()`
   (also invoked unconditionally at the top of `LoadAll`) `FreeLibrary`s every
   module and clears `effects_`, while compositor `VideoEffectSession`s and
   audio-mixer `AudioEffectChainSession`s hold `const PluginDesc*` handles and
   `ApplyFn`/`ProcessFn` pointers obtained from `Find`/`ForEffectType`/`OfKind`
   (which return pointers *into* `effects_`). Any reload while sessions are
   alive → dangling `PluginDesc*` + call into unmapped code. `effects_.push_back`
   reallocation during `LoadAll` invalidates the same handles. *Fix*: reserve
   capacity / use `deque`, refcount or mutex-guard module unload until all
   sessions are released, or document+enforce a "no reload while sessions live"
   lifecycle.

3. **CRASH** — `tools/video_editor/ai/audio_pcm.cc:36-41, 55-104` —
   `ExtractMonoPcm`/`ConvertPlanar`/`ConvertInterleaved` trust
   `AudioFrameInfo.data`, `plane_sizes`, `frames`, and `channels` with no bounds
   checks: `plane = data + offset` and `s[i*channels+c]` read past the buffer if
   the decoder reports more frames/channels than the payload holds or
   `plane_sizes` overflow `data.size()`. The media-catalog equivalent
   (`ConsumeAudioBlock`, media_catalog.cc:1394-1441) performs exactly this
   validation — this path was left unprotected. *Fix*: validate
   `f.data.size() >= frames*channels*bps` (interleaved) and
   `sum(plane_sizes) <= data.size()` / per-plane `>= frames*bps` (planar)
   before decoding; reject malformed blocks.

### DATA-LOSS

4. **DATA-LOSS** — `tools/video_editor/project_store.cc:1182-1196` — the
   "crash-safe" publish does `rename(tmp, target)` → on failure
   `remove(target)` → `rename(tmp, target)` again, and on second failure
   *deletes tmp*. Window where neither file exists, and if the second rename
   fails the original project is destroyed **and** the replacement temp is
   removed → total project loss on disk-full/AV-scan/share-violation races.
   *Fix*: use `MoveFileExW(..., REPLACE_EXISTING|WRITE_THROUGH)` (or rename the
   old file to `.bak` first and restore on failure); never delete `tmp` on
   failure — leave it for recovery.

5. **DATA-LOSS** — `tools/video_editor/export_preset.cc:239-254` — identical
   remove-then-rename publish for `export_presets.json`: a failed second
   rename leaves the user's presets gone and `tmp` deleted. *Fix*: same
   atomic-replace approach as above.

6. **DATA-LOSS** — `tools/video_editor/timeline_history.cc:999-1005,
   1065-1069` — `Execute`/`Redo` pop the command, run `Apply`, and *drop* the
   command entirely when `Apply` returns false — it never reaches `undo_` and
   is popped off `redo_`. For `MacroCommand` (line 999) a mid-list failure
   leaves ops `[0, N)` applied with **no undo entry** → half-applied,
   unrecoverable project mutation on every redo of a partially-invalid macro.
   *Fix*: on `Apply` failure push the command back to `redo_` (or into `undo_`
   if partially applied) and/or give `MacroCommand` transaction semantics
   (pre-validate all ops, or roll back ops `[0, N)` on failure).

### LOGIC

7. **LOGIC** — `tools/video_editor/media_prep.cc:137-148, 243-289` —
   `PurgePath` erases `peaks_`/`failed_`/`posters_` for a path but does not
   prevent a worker already in `RunJob` from republishing: `FinishJob`
   (243-289) inserts the just-produced peaks/poster into `peaks_`/`posters_`
   and records failures into `failed_` unconditionally → after purging a
   modified/removed file, stale results repopulate and are never invalidated
   again. *Fix*: keep a `purged_paths_` set (or per-path generation counter)
   checked in `FinishJob` under `mutex_`, and drop results whose generation is
   stale.

8. **LOGIC** — `tools/video_editor/compositor.cc:864-874` — `ApplyClipEffects`
   uses function-local `static bool ocio_loaded` / `static OcioConfig ocio_cfg`
   / `static ocio_assets_dir`: two `Compositor` instances on different threads
   (export worker + preview worker, and export+preview can overlap) race the
   first-time write vs. read of `ocio_cfg` → torn config passed to
   `ApplyOcioOnFrame`. A config-load failure also permanently caches the
   failure for the process. *Fix*: hoist to a Meyers singleton with its own
   mutex, or resolve OCIO config in `Init` before the render loop.

9. **LOGIC** — `tools/video_editor/audio_mixer.cc:1133-1134, 1174-1176` —
   `last_peak_l`/`last_peak_r` are plain floats written inside `GetChunk` on the
   producer thread but read by `LastChunkPeaks()` — documented as a *"UI meter
   accessor"* — so a UI thread polling levels while the worker renders a chunk
   is a data race (UB, can read torn/mid-write values). *Fix*: make them
   `std::atomic<float>` or copy them under `chunk_mutex_` like `last_chunk_`.

10. **LOGIC** — `tools/video_editor/ai/audio_pcm.cc:174-188` — mixed-rate
    sources: `src_rate`/`channels` are latched from the *first* block and later
    blocks at a different rate are appended **un-resampled** (only the
    same-channel/different-rate case is resampled; a rate change between blocks
    silently concatenates). The mixer treats a rate change as fatal
    (`ConsumeAudioBlock`), but this path produces wrong mono PCM/duration for
    e.g. whisper input. *Fix*: match the mixer — fail on rate/channel change
    between blocks.

11. **LOGIC** — `tools/video_editor/extensions/ext_host.cc:308` — `Scan()`
    clears `entries_` but never unloads `loaded_`: rescanning after an
    extension is removed/renamed leaves the old `Ext` loaded, its
    `xer_ext:<id>` module factory registered, and `Run(id)` still callable →
    stale extension executes on the new scan's manifest list. *Fix*: on rescan,
    remove `loaded_` entries (and `engine->RegisterModule` overrides) for ids
    no longer present.

12. **LOGIC** — `tools/video_editor/io/oiio_image.cc:271, 339` —
    `std::filesystem::exists(yaml_path)` is called *outside* the `try` in both
    `LoadOcioConfig` and `LoadImageOcioConfig`; a path containing invalid
    filesystem chars throws `std::filesystem_error` that escapes past the
    guarded OCIO calls → exception out of a `bool`-returning config API into
    the compositor. *Fix*: use `std::error_code` overloads or move the check
    inside the `try`.

13. **LOGIC** — `tools/video_editor/generated_source.cc:172-187` — text
    height `lines.size() * kLineAdvance * scale` and width are computed in
    `int` before the `kMaxGeneratedDim` clamp; a params string with ~10⁵
    newlines (a crafted `.xerp` text param) overflows to a negative `text_h` →
    clamps to 1px instead of rejecting → silent 1-pixel "text" clip.
    *Fix*: compute in `int64_t`, reject oversized text before clamping.

### LEAK

14. **LEAK** — `tools/video_editor/playback/wasapi_sink.cc:199-204` —
    `Close()` run on a thread different from `Open()` skips `CoUninitialize`
    (guarded by `com_thread_` comparison) but still clears `com_init_` → the
    COM init count taken on the opening thread is never released; repeated
    cross-thread close/leaks the apartment refcount (and keeps the apartment
    alive). *Fix*: drain the uninit onto the owning thread (post a task /
    perform it in `Join`-adjacent code on the worker) or keep `com_init_` true
    and uninit from the correct thread in the destructor.

15. **LEAK** — `tools/video_editor/extensions/ext_host.cc:417` —
    `LoadNative`'s `HMODULE h` is used for `xer_ext_init` and then abandoned —
    never stored, never `FreeLibrary`d, and `LoadNative` can be invoked once
    per `loaded_` member → per-load module refcount leaks for the process
    lifetime. *Fix*: store the handle on `LoadedExt` and free in `Scan`/
    destructor unload.

### STUB

16. **STUB** — `tools/video_editor/media_decoder.cc:52-55` +
    `tools/video_editor/media_catalog.cc:810-812` — both still-image fallbacks
    call `Io::OiioHandlesExtension(path.extension().string())`, but
    `extension()` returns `".exr"` **with the dot** while `kExts`
    (`io/oiio_image.cc:216-225`) holds `"exr"` without it → the lookup can
    never hit: the entire OIIO still-image path (EXR/DPX/TIFF/RAW/PSD/HDR
    import **and** decode) is dead code — stills fail probe and can't be added.
    *Fix*: strip the leading `.` (`ext.substr(1)`) before lookup, or store
    dot-prefixed names in `kExts`.

## Notes

- Reviewed and considered clean: `compositor.cc` core loop (canvas/fps
  validation, `kMaxDecodeSteps` cap, guarded blend/effect/mask paths),
  `playback/preview_engine.cc` (single worker, mutex-protected commands,
  session reopen on discontinuity), `playback/frame_clock.cc`,
  `project_autosave.cc` (rotating slots + `error_code` I/O),
  `io/oiio_image.cc` Read/WriteImage (dimension/channel validation, RAII
  close), `vst3_chain.cc`/`vst3_params.cc`/`vst3_factory.cc`,
  `lab/object_ops.cc` (rects go through `ClampRect`; hostile-rect arithmetic
  stays in-bounds), `lab/mask_refine.cc`, `lab/mkl_accel.cc`,
  `lab/async_op.cc`, `lab/color_pick.cc`, `ai/whisper_transcribe.cc`,
  `ai/beat_markers.cc`, `keyboard_map.cc`, `sequence_preset.cc`,
  `bitmap_font.cc`, `export_engine.cc` (atomic temp+commit mux path,
  cancel-safe chunked providers), `media_prep.cc` worker lifecycle,
  `timecode.cc` (exact rational `SecondsToFrames`; documented non-drop-only
  parse), `script_host.cc` (path sanitizer, error escaping), `main.cc`
  (UTF-16→UTF-8 argv, CEF early-out), `poster_cache.cc`/`clip_poster.cc`.
- Not run: a `windows-clang-msys2` preset build (audit-only pass); findings are
  source-confirmed.
