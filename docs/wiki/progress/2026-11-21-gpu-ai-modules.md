# 2026-11-21 — GPU/AI module wave + NLE research

Five requested modules landed for video_editor; smoke `SMOKE RESULT
OK` (~43 checks). Uncommitted in working tree as of 2026-11-21
(mixed with prior render-perf/records/project-manager work).

## Modules (all delay-load, graceful-degradation)

| Module | Where | Contract |
|---|---|---|
| `YA.CUDADecode` | `src/gpu_video/cuda_decode.*` | FFmpeg `hwaccel=cuda` decode path; runtime probe: nvcuda.dll + device ctx + ffmpeg hwaccel list |
| `YA.CUDAEncode` | `src/gpu_video/cuda_encode.*` | nvenc resolution (`h264_nvenc`/`hevc_nvenc`/`av1_nvenc`) for export presets |
| `YA.FrameRender` | `src/frame_render/frame_render.*` | frame→surface seam: RenderStore transport + CPU normalize path verified; Vulkan loader detected |
| `YA.AIFindObject` | `src/content/ai/find_object/` | ONNX (YOLOv8n-style `[1,3,H,W]`→NMS) via ORT C API or OpenCV DNN fallback; model discovery `$XER_FIND_OBJECT_MODEL`→`assets/models/`→`models/` |
| AI tags layer | `tools/video_editor/ai_tags.*` + Clip `"tags"` field | Async Find Objects over clip frames → `TagRange{start,end,label,conf}` serialized in `.xerproj`; UI: clip ctx menu + inspector AI TAGS section + timeline tag ticks |

## Verified availability on dev machine (RTX 3070)

- `nvcuda.dll` + CUDA device ctx: **yes**; ffmpeg `hwaccel=cuda`: yes;
  `cuda_decode` produced `first_frame_surface=cuda` on a real decode.
- `h264_nvenc` resolvable and usable.
- ggml opencl sees `NVIDIA GeForce RTX 3070 (OpenCL 3.0 CUDA)`.
- CUDA toolkit headers: **not installed** → `src/wrapper/cuda` bridge
  stays disabled; driver-level + FFmpeg paths work regardless.
- `onnxruntime.dll` in MSYS2 clang64 is **broken** (wants
  `libabsl_*-2605`, toolchain ships 2608) → OpenCV DNN is the active
  AIFindObject backend; ORT probe reports "linked, unloadable".
- Real inference unverified — no ONNX model file present.

## nn_probe hardening (same wave)

`NnBackendRuntimeProbe` LoadLibrary-probes delay-imports before first
call (binder's `0xc06d007e` is uncatchable); fixed ggml ctx-pool
null-deref and ggml-blas device-0 `GGML_ABORT` (now prefers CPU
device + `dev_supports_op`).

## Prior waves landed in the same tree (uncommitted)

- Render-perf agent: direct2d_renderer/widget_tree/window edits —
  landed before agent death; content unreviewed.
- ProjectManager (`ui/project_manager.*`) + `records_store.*` —
  source landed; startup-screen wiring unverified.
- `nn_probe.*` — backend availability matrix in smoke.

## Research filed

`research/2026-11-21-nle-architectures.md` — MLT/Olive/Blender/
Frei0r/OFX/libplacebo survey; top-3 calls: hash-keyed frame cache +
AutoCacher (Olive pattern onto MediaPrep + dormant `ClipFrameCache`
.dxcfz), libplacebo+hwaccel GPU path, Frei0r now/OFX at Motion.

## Debt

- Commit `bc1f3a2` contained uncompilable `ReplaceClip` call sites —
  fix (BoundsForPath args) is uncommitted.
- Smoke watchdog raised 15s→240s (was firing mid-suite).
- `out/` should be excluded from Windows Search indexing (transient
  link locks).
- Whole tree needs one commit sweep + message.

## Follow-up same day — video_editor launch crash fixed

`video_editor.exe` AV'd ~1s into the first paint:
`0xc0000005` inside `d2d1.dll` under
`Direct2DRenderer::DrawText` ← `TextInput::Render` (inspector
TRANSFORM fields). Bisected via env gates left by the dead perf
agent: the **IDWriteTextLayout LRU cache** (73bc94f) is the trigger —
`XER_DT_NOCACHE=1` survives indefinitely, cached path crashes.
`DRAW_TEXT_OPTIONS_CLIP` is innocent (crash repro'd with NONE).

Shipped fix (uncommitted, `direct2d_renderer.cc`): cache gated
behind opt-in `XER_DT_CACHE=1`, default OFF; all TEMP-DIAG
fprintf/env branches removed. Open question for a debugger session:
whether reuse fails on retained `ID2D1Bitmap1` canvas targets only
(canvas + cache interaction is the plausible mechanism — the cache
predates the crash, the canvas does not).
