# Video Editor: JavaScript effects & transitions

Scripts under `assets/scripts/fx/` extend the render graph without
recompiling. The catalog is `fx.json` (pure JSON — UI lists work
without V8); each `.js` file registers itself through a global the
per-thread runtime installs before evaluation:

```js
xerFxVideo({ id, title, apply(frame, params, ctx) });
xerFxAudio({ id, title, process(pcm, params, ctx) });
xerFxTransition({ id, title, weight(progress, ctx) });
```

- `frame` = `{ data: Uint8ClampedArray(BGRA), width, height, stride }`
- `pcm` = `Float32Array`, interleaved stereo `[-1, 1]`
- `ctx` = `{ time }` (+ `sample_rate` for audio)

Sandbox note: `V8_ENABLE_SANDBOX` forbids non-owning backing stores,
so buffers are V8-allocated and round-trip per call (one memcpy each
way — bounded by frame size).

## Wiring

| Surface | Convention | Engine |
|---|---|---|
| `ClipEffect.type == "script:<id>"` | compositor effect chain | `JsFxRuntime::ApplyVideo` |
| `ClipAudioFx.plugin_path == "script:<id>"` | audio_fx after the VST3 chain | `JsFxRuntime::ProcessAudio` |
| `Transition::Type::Script + .script` | easing curve; renders as crossfade | `JsFxRuntime::TransitionWeight` |

`tools/video_editor/js_fx.{h,cc}` holds the runtime. One
`JsFxRuntime` per calling thread (V8 isolates are thread-confined) —
the compositor and each mixer own theirs; the JSON catalog is
process-global and load-once. Failures warn once and degrade to
identity (frame/pcm untouched, linear transition weight).

Serialization: `project_store` writes `"type":"script"` +
`"script":"<id>"` for transitions; `ClipEffect.type` and
`ClipAudioFx.plugin_path` carry their `script:` prefixes as plain
strings — undo/redo rides the existing full-snapshot commands.

Script authoring: see `tools/video_editor/assets/scripts/fx/` —
14 stock entries (5 video, 4 audio, 5 transitions). Params arrive as
an object keyed by manifest `params[].id`; positional ClipEffect
params map by index, ClipAudioFx params by manifest ordinal.

# Video Editor: media decode subprocess

`video_editor --media-worker` re-execs the same binary as a headless
JSON-lines decode service. `MediaPrep::RunJob` routes `kPosterAt` and
`kPeaks` through `MediaWorkerClient::Shared()` first — a crashing
decoder (malformed files, FFmpeg edge cases) kills the worker, not
the editor — and falls back to the in-process decode on spawn
failure, timeout (poster 15 s, peaks 120 s), worker death, or a
"suspect" path (a path whose request killed the last worker;
`kMaxDeaths = 3` total respawns before the client stays in-process).

`kImportPoster` stays in-process: the poster row update lives in the
parent's SQLite catalog. `XER_VE_MEDIA_WORKER=0` forces the
in-process path. Protocol + base64: `media_worker_protocol.{h,cc}`;
worker loop: `media_worker_server.cc`; spawn/pipe client:
`media_worker_client.cc` (Windows-only for now, POSIX falls back to
in-process).
