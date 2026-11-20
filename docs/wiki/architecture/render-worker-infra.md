# Render worker infrastructure (UiRenderHost + RenderStore)

How process-separated UI rendering works. All claims as of
2026-11-20, verified in the working tree (uncommitted changes on
top of `65cd81b`).

## Shape

- `xer_engine_ui_render.exe` (`tools/XERUIRender/main.cc`) is the
  render worker. The app renders via
  `Rendering::StreamRenderer` which serializes every `Renderer` call
  into ops over a `Cache::SharedMemoryChannel`; the worker replays
  them onto a `Windows::Direct2DRenderer` (D3D11 + DXGI + D2D1).
- `Core::UiRenderHost` (`src/ui/core/render_host.*`) owns a pool of
  workers (`xer.ui.render.<pid>.<id>` channels), spawned eagerly by
  `Application::Initialize` (default 2; `XER_UI_RENDER_WORKERS` =
  N / `auto` → cores/4 ≤ 8 / `0` = off). `CreateRemoteRenderer`
  checks out a warm worker and retargets via `kSetTargetHwnd`;
  `ReapAndRespawn` refills dead pool slots.
- Partition: `YA.UIRenderIPC` owns `render_stream.cc`,
  `render_store.cc`, `render_host.cc`. `YA.UICore` links it.

## RenderStore (added 2026-11-20)

Named shared-memory segment `xer.ui.res.<pid>` for payloads too big
for the channel — images, video frames, audio levels. 256 slots,
bump arena, seqlock reads (seq odd = write in flight). Default
capacity 64 MB, `XER_UI_STORE_MB` overrides. Windows
`CreateFileMapping`; POSIX `shm_open` fallback exists but the pool
itself is Windows-only.

Host API: `UiRenderHost::UploadImage` / `WriteVideoFrame` /
`WriteSpectrum` / `FreeResource` (broadcasts `kFreeResource`).
Workers attach via `kAttachStore` emitted at checkout; dispatch
resolves ids through `handlers.on_resource` into
`Draw*Pixels` calls on the renderer.

## Stream ops added 2026-11-20

`kAttachStore`, `kDrawResource` (static image by id),
`kDrawVideoFrame` (live frame by id), `kDrawSpectrum` (float
levels → bars, modes 0/1/2), `kFreeResource`, `kPushLayer` /
`kPopLayer` (offscreen layer, opacity), `kSetTransform` /
`kResetTransform` (2x3 affine).

`WidgetTree::RenderSubtree` emits layer/transform ops automatically
from computed style — `opacity`, `filter: blur()`,
`transform: translate()/scale()` — so per-element effects are
evaluated inside the worker.

## Constraints / traps

- D2D `PushLayer` applies opacity only; `blend_mode` and `blur`
  params are accepted but ignored (need `ID2D1Effect` chains).
- `RenderStore` is a grow-only arena — `Free(id)` marks the slot
  dead but does not recycle memory; a full store fails `BeginWrite`.
- `StreamRenderer::DrawFrameBgra` still sends pixels inline — fine
  for thumbnails, wrong for real video; use `WriteVideoFrame` +
  `DrawVideoFrame` for live sources.
- Tests: `tests/ui/render_store_tests.cc` (5 tests, store
  round-trip + op dispatch).

## Renderer provider model (2026-11-20, agent e0aa8a8f)

- `src/ui/rendering/renderer_provider.h`: `RendererBackend`
  {kAuto,kSoftware,kD2D,kD3D11,kVulkan} + `CreateRenderer` /
  `CreateRendererFromEnv` (`XER_UI_RENDERER=...`, fallback chain
  D2D→Vulkan→Software). `kD3D11` aliases the D2D renderer (it owns
  the D3D11 device already).
- `rendering/vulkan/vulkan_renderer.{h,cc}` — real immediate-mode
  backend: no SDK needed (skia-vendored headers + vulkan-1.dll
  dynamically loaded), 3 pipelines (color/texture/text-mask),
  staging-buffer BGRA uploads, deferred post-fence destruction.
  Deferred: rounded corners, blend modes, real frosted blur.
  Verified live on NVIDIA RTX 3070 ICD (acquire→submit→present).
- `rendering/windows/software_renderer.{h,cc}` + `raster_util` —
  retained CPU buffer, SDF AA rects, GDI text masks, WIC decode.
- `window.cc` renderer sites → `CreateRendererFromEnv`; UI-render
  worker replays onto provider-chosen backend.
- Full op coverage matrix in `src/ui/ARCHITECTURE.md`.
