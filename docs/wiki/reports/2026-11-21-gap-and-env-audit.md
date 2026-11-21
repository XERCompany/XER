# Audit: remaining VE gaps + dev-environment (2026-11-21)

Two parallel read-only subagent audits. Sources: this session's
subagent transcripts. Verdict-level summary; file:line refs are as of
`5a82ec8`.

## A. Code gaps (ranked by user impact)

**Bugs worth fixing first**
1. `SyncAudioClips` decodes synchronously on the UI thread
   (`audio_sync.cc:174` raw `DecodeAudioPeaks` from
   `video_editor_app.cc:1894`) — freezes UI on real footage, defeats
   the media-worker crash isolation, and each match is a separate
   undo step. `BestLag` O(n·m) uncapped.
2. JS fx hazards: no runaway-script guard (`while(1){}` freezes
   compositor/mixer); `FormatV8Error` `ToLocalChecked` on possibly-
   empty SourceLine (`js_fx.cc:194`) → crash while reporting an
   error; one `process` fn per id → stateful audio fx (delay/lowpass
   closures) bleed across clips; preview/export state divergence.
3. A/V-path demux pump ignores the audio-tap high-water mark
   (`ffmpeg_video_decoder.cc:574-582` vs fixed audio path `:443-449`)
   — silent packet drops still possible on fast pumping.
4. `MediaWorkerClient::RoundTrip` serializes entire request →
   head-of-line blocking; pipe-handle leak on second CreatePipe fail;
   racy lazy init.
5. `bokeh_dof`/`bokeh_blur` = 24 taps/pixel — full-frame 1080p won't
   hold preview realtime; needs quality-scaled preview or GPU port.

**Missing UX basics**: no unsaved-changes prompt (New/Open/close),
no OS drag&drop import, no bin→timeline / cross-track drag, no
marker next/prev keys, project settings bypass undo, dockable
panels + remappable shortcuts still queued.

**Coverage gaps**: audio_sync zero tests; media_worker_protocol
untested; KeyEase unit tests absent; js_fx error paths untested.

## B. Environment ("pit of success")

1. `Build.ps1` has no pre-build lock-kill — repeated CEF/link failures
   this session from running exes. (S)
2. CI builds the preset but never runs `video_editor --smoke` or
   `tools/check_all.ps1` — smoke coverage is zero in CI. (S)
3. Missing smoke checks: `probe_classification` (seam exists:
   `IsAttachedPictureStream`), `poster_cache_lru`, `assets_icons`;
   `audio_pump` fake-clock 90s overrun check (M). (S/M)
4. `main.cc:99` PrintUsage omits `--audio-probe`, `--media-worker`,
   `--cef-check`; no single `--diag` bug-report mode. (S→M)
5. Wiki rot: `build-and-test-infra.md` lines 1-100 describe the dead
   vcpkg/MSVC world — needs a superseded banner; missing pages:
   windows-msys2-gotchas, crash-debugging-windows. (S)
6. Two duplicate SEH tracers (`main.cc:653`, `test_main.cc:10`);
   `src/app/crash_handler.cc` is the fuller unlinked implementation.
   (S)
7. `CopyDirRetry.cmake` FATAL never names the locking process. (S)
8. No `--screenshot` headless UI capture — manual visual check is the
   only way to verify flicker/layout; UiEventBus kAfterFrame hook +
   ImageSaver::SavePng plumbing exists. (M/L)

## C. Verified fixed (skip re-checks)

Media-prep worker live, transition pickers, JKL→SetRate, Q/W+`,/.`+
Ctrl+D, Source Monitor, poster LRU+320px cap, ~30s audio drop,
mp3 cover-art, WASAPI/COM, export-on-worker.
