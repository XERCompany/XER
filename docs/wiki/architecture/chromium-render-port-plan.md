# Chromium render/compositing → XER VideoEditor render path port plan

Research + port plan dated 2026-11-24. Scope: the **render/media
pipeline** level (decode → composite → present). Companion doc
[chromium-views-port-plan.md](chromium-views-port-plan.md) covers the
widget level (View↔Widget, layout, focus) — nothing below duplicates it;
its one line that matters here is the shared verdict "our UI is
immediate-mode, theirs is retained layers", which this plan does NOT try
to overturn.

Local sources read: `tools/video_editor/compositor.{h,cc}`,
`playback/{preview_engine,frame_clock}.{h,cc}`,
`ui2/{monitor_surface,preview_binding,ve_app}.{h,cc}`, `main.cc`,
`src/video/{frame,rendering_engine,angle_renderer,clip_gpu_accel,
rendering_engine}.h`, `src/frame_render/frame_render.h`,
`src/gpu_video/cuda_decode.h`, `src/wrapper/ffmpeg/ffmpeg_video_decoder.cc`,
`src/wrapper/{angle,skia}/`, `src/ui/canvas/canvas.{h,cc}`,
`src/ui/rendering/{renderer.h, windows/{direct2d_renderer,d2d_presenter,
software_renderer}.cc, vulkan/}`, `src/media/` (whole tree),
`docs/wiki/architecture/{video-pipeline-facts,static-link-state,
render-worker-infra}.md`. Chromium sources fetched from
chromium.googlesource.com @main: `media/base/{video_frame.h,
video_renderer_sink.h, media_resource.h, time_source.h}`,
`media/filters/{video_renderer_algorithm.h, audio_clock.h}`,
`media/renderers/video_renderer_impl.h`, `media/base/pipeline.h`,
`media/gpu/windows/d3d11_video_decoder_wrapper.h`,
`gpu/command_buffer/{client/shared_image_interface.h,
service/shared_image/shared_image_manager.h}`,
`components/viz/{common/frame_sinks/begin_frame_source.h,
service/display/display_scheduler.h}`.

Binary-level verification (this machine):
`llvm-readobj --coff-exports` on `clang64/bin/libskia.dll` → Ganesh
**GL-only**: `GrDirectContexts::MakeGL` + `GrGLAssembleInterface`/
`GrGLInterface` exported (405 Gr*Context/Backend symbols); **no
MakeD3D, no MakeVulkan, no Graphite**; D3D header dir ships only
`GrD3DBackendContext.h`/`GrD3DTypes.h` (no `GrD3DDirectContext.h` →
`canvas.cc`'s `XER_SKIA_HAS_GANESH_D3D` gate is OFF → canvas is
raster-only today). `libEGL.dll`/`libGLESv2.dll` staged in bin/ are
**real ANGLE** (ANGLE-only exports: `eglForceGPUSwitchANGLE`,
`eglCreateStreamProducerD3DTextureANGLE`) on a D3D11 backend.
`ffmpeg -hwaccels` on our build: `cuda vaapi dxva2 qsv d3d11va vulkan
d3d12va amf` — D3D11VA is already compiled in.

## 0. The one-paragraph model difference

Chromium's render path is **pull-based and vsync-phase-aligned**: a
`BeginFrameSource` fires `BeginFrameArgs{frame_time, deadline,
vsync_interval}` at vblank; `DisplayScheduler` aggregates damage and
asks each `VideoRendererSink::RenderCallback` to `Render(deadline_min,
deadline_max)` — the renderer *pulls* the frame whose PTS covers the
display interval out of a bounded queue filled by decoders running
ahead; GPU-side everything is a `VideoFrame` wrapping a
`ClientSharedImage` (texture) + `Mailbox` + `SyncToken`. XER's path is
**push-based and wall-clock**: `PreviewEngine`'s worker computes
`t = anchor + wallΔ·rate`, sleeps `frame_dt/|rate|`, renders a full
BGRA canvas on CPU, pushes it through 2 memory copies into a
`WM_TIMER`-driven widget tree, which uploads it to a D2D bitmap at
paint time. **Every hop below filters through that difference.**

## 1. Mapping table

| Chromium | XER | Verdict |
|---|---|---|
| `media::VideoFrame` (StorageType: OPAQUE/OWNED_MEMORY/SHMEM/DMABUFS/MAPPABLE_SHARED_IMAGE; `WrapSharedImage(ClientSharedImage, ReleaseMailboxCB, …)`; visible_rect/coded_size/metadata) | `video::Frame` (`src/video/frame.h`): `std::vector<uint8_t>` CPU pixels only, `PixelFormat{BGRA8,RGBA8,NV12,YUV420P}`, `pts` µs | Present but **storage-type-blind**: no texture/handle representation, no release callback, no visible_rect. The `GpuVideo::FrameSurface{kCpu,kCuda}` marker + `LastFrameSurface()` (cuda_decode.h:63,121) is our dormant seam for exactly this. |
| `gpu::ClientSharedImage` + `Mailbox` + `SyncToken` (cross-process texture + ordering) | `RenderStore` shared-memory slots (`src/ui/rendering/render_store.h`, slot 240 for video) + `FrameRender::Service` transport | Conceptually analogous (a handle the producer writes and consumer resolves), but ours is **CPU shared memory for remote worker processes**, not a GPU texture. In-process GPU zero-copy needs D3D11 shared handles + keyed mutex instead — which `D2DPresenter` already implements (below). |
| command_buffer client→service stream, GPU process | none — single process + optional `xer_engine_ui_render.exe` workers | **Not portable, don't port** (see §5). |
| `viz::Display` + `DisplayScheduler` + `DisplayCompositor` (aggregates surfaces, issues draw+swap per BeginFrame) | `Direct2DRenderer` (flip-model swapchain + retained `canvas_bitmap_`) + `D2DPresenter` thread | Present. `D2DPresenter` already does the Chromium-style producer/consumer handoff: UI-thread device A draws a **keyed-mutex shared texture**, present-thread device B `OpenShared`+copies+`Present1` (d2d_presenter.h:14-19). This is our miniature "GPU process". |
| `BeginFrameSource` / `BeginFrameArgs{frame_time,deadline,vsync_interval}` / observer ack backpressure (`DidFinishFrame`) | `Window::SetFrameCallback` → `SetTimer(kFrameTimerId, 33ms)` (+`NtSetTimerResolution` 0.5ms) → WM_TIMER → `VeApp::TickTree` → `DrainPending` → `Invalidate` → WM_PAINT (window.cc:517-550, 905-933) | Gap. Timer tick ≠ vsync; no deadline, no sequence, no backpressure, no drop accounting. D2DPresenter's `Present(0)` is vblank-serialized by DXGI but the *UI cadence* stays timer-driven → paint/present phase drift. |
| `VideoRendererSink::RenderCallback::Render(deadline_min, deadline_max, RenderingMode)` (pull per vsync; `requestVideoFrameCallback` rides this) | `MonitorSurface::SetFrameProvider(fn)` pull path + `FrameRender::Service::Present` | Partial: we have the pull *shape* but it's consulted per-paint with no deadline interval — the provider returns "latest", not "frame due at [t_min,t_max]". |
| `VideoRendererAlgorithm` (frame_queue_, integer cadence via `VideoCadenceEstimator`, drift-based pick, `frames_dropped`, `RemoveExpiredFrames`, `effective_frames_queued`) | none — `PreviewEngine` renders exactly the frame at snapped `tr` each tick; no queue, no decode-ahead buffer, drop = "playhead silently skips" | **Missing entirely** — biggest correctness/perf gap in playback. |
| `TimeSource` + `AudioClock` (media time = `front_timestamp()` = Σ frames_written/rate scaled by playback_rate; video pulls frames due at media time) | wall-clock anchor in PreviewEngine worker; audio paced by blocking `IAudioSink::Write` + `lead_chunks` pump | Gap. Our two clocks are **independent**: video from wall clock, audio from sink writes → drift is unbounded. |
| `media::Pipeline` stage graph: `Demuxer→DemuxerStream→DecoderStream→RendererImpl` (audio+video sharing one TimeSource), buffering state machine | monoliths: `VideoDecoder` = demux+decode in one class; `Compositor` owns per-clip decoders + effects + blend; `PreviewEngine` owns pacing | Same boundaries exist only in `src/media/` **skeleton** (design-doc headers: `MediaResource`, `DecoderStream`, `VideoRendererImpl`+`VideoFrameCompositor`+`TimeSource`, `gpu/VideoDecodeAccelerator`) — none of it wired into the editor path. |
| `D3D11VideoDecoder`/`D3D11VideoDecoderWrapper` + `D3D11VideoProcessorProxy` (NV12 texture → BGRA texture on GPU) | `CudaVideoDecoder` (cuda hwaccel → `av_hwframe_transfer_data` **downloads to CPU**); `ffmpeg_video_decoder` = sws_scale → BGRA | Dormant: FFmpeg build has `d3d11va`/`d3d12va`/`dxva2` compiled in; our decoders always land on CPU. |
| Skia Ganesh GL/Vulkan/D3D + Graphite (GPU record+execute) | `libskia.dll` = Ganesh **GL only**; `canvas.cc` uses raster `SkSurfaces::Raster`; `skia_engine_bridge` = CPU pixel ops | Raster-only today. GPU path viable via **ANGLE**: `eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, D3D11)` → `GrGLMakeEGLInterface` → `MakeGL` (all exports/headers verified present). |
| `content/` renderer process: VideoFrameProvider/WebMediaPlayer + rVFC | `PreviewBinding::Sink` → `DrainPending` → `MonitorSurface::SetFrame` | Analog exists (worker→UI marshal), but two full-frame memcpys instead of one mailbox handoff. |

## 2. Current CPU-copy audit & cost

Chain per displayed frame (verified in source):

1. sws_scale → BGRA in decoder (YUV→BGRA CPU convert): ~2–5 ms @1080p, ~10–20 ms @4K
2. `Compositor::RenderFrameAt` → `out_bgra` vector: per-clip bilinear warp (`WarpClipFrame`, compositor.cc:207-244 — nested x/y per-pixel loop, **the most expensive op**, tens of ms @4K) + transitions + blend
3. `PreviewEngine::RenderAndDeliver` → `memcpy(out_frame_.Data(), bgra_, …)` (preview_engine.cc:468) — copy A, 8.3/33 MB
4. `PreviewBinding::Sink::OnVideoFrame` → `pending_frame_.assign` (preview_binding.cc:55) — copy B
5. `MonitorSurface::SetFrame` → `make_shared<vector>(bgra)` (monitor_surface.cc:120) — copy C
6. paint → `Direct2DRenderer::DrawFrameBgra` → `ID2D1Bitmap1::CopyFromMemory` (direct2d_renderer.cc:1058) — CPU→GPU upload D
7. `D2DPresenter` shared-texture copy → swapchain → `Present1` (GPU-side, cheap)

Copies A–C ≈ 3×8.3 MB @1080p ≈ 3–6 ms/frame of pure memcpy, ~15–25 MB/frame of bandwidth at 30fps; at 4K (33 MB each) ≈ 10–20 ms/frame — **alone exceeding a 33 ms budget** once compositor pixel loops are added. GPU upload D is unavoidable in some form but should happen **once**, as the only upload.

## 3. Zero-copy path design (single-process Chromium recipe)

Chromium's decode→composite zero-copy = "decoder writes D3D11 texture
→ wrap as SharedImage → compositor samples it". In one process the
SharedImage/Mailbox/SyncToken machinery collapses to: **one shared
D3D11 device + `IDXGIKeyedMutex` (or NT-handle shared textures) for
ordering** — the pattern `D2DPresenter` already runs.

```
┌─ decode ──────────────────────────────────────────────────┐
│ FFmpeg hwaccel d3d11va (already compiled):                 │
│   av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_D3D11VA, our_dev)│
│   → AV_PIX_FMT_D3D11 hw_frames_ctx → ID3D11Texture2D(NV12) │
│   (cuda path in gpu_video/ stays as NVIDIA alternative)     │
└────────────┬──────────────────────────────────────────────┘
             │ NV12 texture on shared D3D11 device  (no download!)
┌────────────▼──────────────────────────────────────────────┐
│ color convert (pick one):                                  │
│  a) ID3D11VideoProcessor (Chromium d3d11_video_processor_  │
│     proxy equivalent): NV12→BGRA texture, handles BT.601/  │
│     709 + range — the honest NLE-grade converter            │
│  b) trivial YUV→BGRA pixel shader via ANGLE GLES           │
└────────────┬──────────────────────────────────────────────┘
             │ BGRA texture
┌────────────▼──────────────────────────────────────────────┐
│ GPU compositor (new, §5-R5): per-clip SkImage wrapping the │
│ texture (SkImages::AdoptTextureFrom/BorrowTextureFrom via  │
│ Ganesh-GL, importing the D3D11 tex through ANGLE's         │
│ eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_ANGLE)) — │
│ or our own GL quads. Warp=transform draw; src-over/blend = │
│ blend state; masks/transitions = fragment ops. Output:     │
│ one canvas-sized BGRA texture.                             │
└────────────┬──────────────────────────────────────────────┘
             │ shared-handle / keyed-mutex
┌────────────▼──────────────────────────────────────────────┐
│ present (pick one):                                        │
│  a) Skia Ganesh-GL surface on ANGLE window surface bound   │
│     to the monitor child HWND (Canvas::Impl::Gpu path,     │
│     re-cut for GL since D3D headers absent)                │
│  b) share the composed texture back to Direct2DRenderer's  │
│     D3D11 device via keyed mutex → DrawImage into canvas   │
│     bitmap → existing Present1 dirty-rect pipeline         │
└────────────────────────────────────────────────────────────┘
```

Fallback at every hop: hwaccel negotiation fails → today's CPU BGRA
path unchanged (already the `CudaVideoDecoder` contract: "worst case
is the plain CPU path").

## 4. Frame scheduler: what Chromium has that we lack

| BeginFrame/viz capability | Our pump | Gap |
|---|---|---|
| vsync-phase-aligned callback (`frame_time` = vsync tick) | `SetTimer` 33 ms + `NtSetTimerResolution(0.5ms)` (window.cc:912-930) | Timer fires at arbitrary phase vs vblank → jitter even with 0.5 ms resolution; paint completes mid-interval and sits a full vsync |
| `deadline` per frame → `Render(deadline_min, deadline_max)` picks frame covering the *display* interval | snapped `tr` from wall clock at *render* time | We select by when we paint, not when it displays; on jitter we can show the wrong frame slot |
| Backpressure: observer ack (`DidFinishFrame`), `MaxPendingSwaps`, GPU-busy withholding | none; worker free-runs, D2DPresenter silently coalesces (newest wins) | Render worker can lap the display; wasted renders invisible except indirectly |
| Drop accounting (`frames_dropped`, `RemoveExpiredFrames`, `OnLastFrameDropped`) | none — skipped frame slots aren't counted; worse, the compositor **still decodes through** skipped PTS (sequential decode) | Burned decode work on frames never shown; no metric to even see it |
| `GetPreferredRenderInterval` — sink adapts pump cadence to media fps | worker always sleeps `frame_dt/|rate|` | Fixed 30 Hz pump vs 24/25/60 fps media → uneven cadence (mitigated only by PTS snapping) |

Adoption sketch: a `BeginFrame`-lite = presenter thread waits
`IDXGIOutput::WaitForVBlank` (or DwmFlush) → posts the tick that drives
`DrainPending`+`Invalidate`, so each WM_PAINT lands inside the vsync
interval it targets; PreviewEngine exposes `Render(t_min,t_max)` and a
per-clip decoded-ahead queue.

## 5. media/pipeline lessons → tickets

**A/V sync math (portable, high value).** Chromium drives everything
off one `TimeSource`; audio implementation = `AudioClock` where
`front_timestamp = base + Σ(frames_written·playback_rate)/sample_rate`.
Our equivalent is already computable: `audio_next_index_·chunk_dur` in
`PumpAudio` (preview_engine.cc:524-541) *is* the audio clock. Video
should pick frames due at that time instead of raw wall `t` — a 20-line
change to `RenderAndDeliver`'s `tr` computation plus drift correction.

**Dropped-frame policy (portable).** VRA: bounded queue
(`min_buffered_frames_`/`max_buffered_frames_`), decoder reads ahead
(`AttemptRead_Locked`), `Render` picks the frame whose PTS interval
covers the deadline window, skips = `frames_dropped`. Ours: decode is
demand-driven per tick and strictly sequential — no read-ahead depth,
so a slow render both drops *presentation* slots AND makes the decoder
chew frames nobody sees. Port as: per-ClipSession ring (already 3-slot)
extended to a read-ahead queue keyed by PTS + a `PickFrame(t_min,t_max)`
chooser.

**Stage boundaries (partially portable).** Chromium: demux
(MediaResource→DemuxerStream) separate from decode (DecoderStream)
separate from render (sink+clock). Ours: `VideoDecoder` fuses
demux+decode (audio tap rides the video pump — the starvation rule in
video-pipeline-facts.md), `Compositor` fuses decode-scheduling + effects
+ blending, `PreviewEngine` fuses clock + pacing + audio pump. The
`src/media/` skeleton already sketches the right split — but it's an
unused design doc; wiring the editor onto it is **L** and should follow,
not precede, the zero-copy work.

**Not portable — say so plainly:** `gpu/command_buffer` (serializing
GPU commands across a process boundary), `SharedImageManager`'s
multi-representation machinery (GL passthrough/Dawn/Vulkan/overlay
backings, purgeable, GpuFence IPC), the GPU-process sandbox, Mojo data
pipes, viz multi-surface SurfaceAggregation, aura. All exist to solve
*cross-process* trust/boundary problems we don't have — our process
topology is one process + optional render workers, where
RenderStore-slots + D3D11 keyed mutexes already provide the same
ordering/lifetime guarantees the Mailbox/SyncToken pair provides them.
Porting command_buffer would be shipping an RPC layer to talk to
ourselves.

## 6. Ranked port tickets

Effort: **S** ≤1 day, **M** 1–3 days, **L** >3 days. Order = value/risk
sequence (each independently shippable, each degrades to CPU path).

| # | Ticket | Files | Effort |
|---|--------|-------|--------|
| R1 | **ANGLE reality check**: turn `Bridge::Angle::IsAvailable()` stub into a real EGL init: `eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, default_display, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE…)` → `eglInitialize` → pbuffer+GLES2 ctx → `glGetString(GL_RENDERER)`. Also record D3D11 device pointers ANGLE hands out via `EGL_D3D11_DEVICE_ANGLE`-style device query | `src/wrapper/angle/angle_engine_bridge.{h,cc}`; probe in `--diag`/`--smoke` | **S** |
| R2 | **Skia Ganesh-GL verification**: with R1's context current, `GrDirectContexts::MakeGL(GrGLMakeEGLInterface())` → `SkSurfaces::RenderTarget` on an EGL window surface; draw + `flushAndSubmit` + `eglSwapBuffers` into a test HWND. Proves GPU Skia on our actual libskia before any porting | `src/ui/canvas/canvas.cc` (new `XER_SKIA_HAS_GANESH_GL` branch alongside the dead D3D one), smoke test | **S** |
| R3 | **GpuFrame**: texture-capable frame type alongside `video::Frame`: `{ID3D11Texture2D* tex, int subidx, or SkImage; pts; visible_rect; ReleaseCB}`; `video::Frame::AsGpu()`/`Download()` lazy conversion keeps every existing CPU consumer compiling. Chromium equivalent: StorageType + `WrapSharedImage` + `ReleaseMailboxCB` | `src/video/frame.h`, new `src/video/gpu_frame.{h,cc}`; consumers touched via `FrameRender::Service` first | **M** |
| R4 | **D3D11VA decode**: `av_hwdevice_ctx_create(AV_HWDEVICE_TYPE_D3D11VA)` on a device owned by a new `GpuVideo::SharedDevice` (the same D3D11 device the compositor/presenter must use — mirror Chromium's single-`MediaFoundation` device rule); `get_format` → `AV_PIX_FMT_D3D11`; deliver `GpuFrame` w/o `av_hwframe_transfer_data`. Software fallback identical to `CudaVideoDecoder` pattern | `src/gpu_video/d3d11_decode.{h,cc}` (new, modeled on `cuda_decode.cc`), `src/wrapper/ffmpeg/ffmpeg_video_decoder.cc` (hwaccel branch), `media_decoder.h` factory hook | **M** |
| R5 | **GPU compositor ops**: port the cheap-ops-first subset of `Compositor` — `WarpClipFrame*` (textured quad + affine matrix), `ApplyBlendModePixels` (GL blend state / Skia blend modes), `ApplyMaskPixels` + `ApplyTransitionPixels` (fragment shaders w/ `w` uniform). Adjustment layer = render-to-texture feedback. Keep CPU `RenderFrameAt` as reference path behind `CompositorOptions.use_gpu` | new `tools/video_editor/gpu_compositor.{h,cc}` (or `src/video/compositing_engine_gpu.cc`); `compositor.h` options | **L** (M for warp+alpha subset) |
| R6 | **Vsync-timed pump**: presenter thread `WaitForVBlank`/`DwmFlush` → `PostMessage(WM_APP+frame)` replacing the 33 ms `SetTimer` when a D2D presenter is attached; keep WM_TIMER fallback. Add `(t_min,t_max)` to the pull-path `FrameProvider` contract | `src/ui/core/window.cc`, `src/ui/rendering/windows/d2d_presenter.cc`, `monitor_surface.h` provider signature | **M** |
| R7 | **Frame queue + cadence + drop counting** (VRA-lite): per-ClipSession read-ahead depth N, `PickFrame(deadline_min,deadline_max)`, `frames_dropped_` counter surfaced to perf HUD | `tools/video_editor/compositor.cc` ClipSession, `preview_engine.cc` | **M** |
| R8 | **Audio-driven media clock**: `TimeSource` impl = `audio_next_index_·chunk_dur` (+sub-chunk interpolation from sink write position); video `tr` derived from it; fall back to wall anchor when audio disabled | `preview_engine.cc`, new `playback/media_clock.{h,cc}` | **S–M** |
| R9 | **Single-copy marshal**: `OnVideoFrame` hands `GpuFrame`/shared `video::Frame` ref through pending slots (drop the `assign` + `SetFrame` copies — B and C in §2) even before GPU lands; `MonitorSurface::SetFrame` overload taking `shared_ptr<const Frame>` | `preview_binding.cc:48-60,374-411`, `monitor_surface.{h,cc}` | **S** |

Sketches:

**R1** — `bool IsAvailable()` becomes lazy-init of:
```cpp
EGLDisplay d = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE,
    EGL_DEFAULT_DISPLAY, d3d11_attribs /* TYPE_D3D11, DEVICE_LATEST */);
eglInitialize(d, nullptr, nullptr);
eglBindAPI(EGL_OPENGL_ES_API);
cfg = choose(PBUFFER|WINDOW, ES2);
ctx = eglCreateContext(d, cfg, nullptr, {CLIENT_VERSION,2});
// interop: eglQueryDisplayAttribEXT(d, EGL_DEVICE_EXT, &egl_dev);
//   eglQueryDeviceAttribEXT(egl_dev, EGL_D3D11_DEVICE_ANGLE, &our_dev)
```
Returns false (and `Summary()` says why) on any failure — preserves
the degraded-machine contract.

**R4** — the critical non-obvious bit: FFmpeg's hwaccel must create its
`AVHWDeviceContext` wrapping **our** `ID3D11Device` (via
`av_hwdevice_ctx_create_derived` / filling `AVD3D11VADeviceContext`),
not an internally-created one — same reason Chromium shares one
`MediaFoundationChannel`. A private decoder device forces a
cross-device copy that reintroduces the very cost we're removing.

**R6** — `D2DPresenter::ThreadMain` already owns the vsync boundary
(presents are flip-queued); emitting `frame_ack` → `PostMessage` after
each `Present1` return gives the UI a real BeginFrame without touching
the message loop.

## 7. Honest verdict

**Genuinely portable, in order of leverage:**
1. GPU frame representation + D3D11VA decode (R3+R4) — kills sws_scale
   + 3 memcpys + the GPU upload, the whole §2 bill; all libraries
   already staged.
2. ANGLE+Ganesh-GL Skia (R1+R2) — verified pairable on today's DLLs;
   turns compositor ops and canvas paint GPU-side.
3. Audio-clock-driven video + frame queue + drops (R7+R8) — pure
   algorithm port, no GPU dependency, fixes real sync drift.
4. Vsync-aligned pump (R6) — small win on jitter; the presenter
   already serializes to vblank so this is polish not correctness.

**Not portable / not worth it:** command_buffer, SharedImageManager
representations, GPU-process/Mojo plumbing, viz SurfaceAggregator,
aura — all solve cross-process problems our topology doesn't have;
RenderStore + keyed-mutex D3D11 sharing are our adequate equivalents.
Also **not worth porting**: Graphite/Dawn (our libskia has neither —
would require a new Skia build, and Ganesh-GL over ANGLE covers the
need), the 29-mode blend parity check (already done via Skia CPU
bridge), and Chromium's decoder-selector/fallback DAG (our
hwaccel→software inline negotiation is simpler and sufficient).

**One-line summary:** Chromium's video path is a pull-from-a-texture
pipeline on a vsync schedule; ours is a push-of-CPU-bytes on a timer.
The portable core is three abstractions — texture-backed VideoFrame,
deadline-driven Render(), audio-derived TimeSource — and they're
reachable entirely with libraries already in `bin/` (ANGLE + Ganesh-GL
Skia + FFmpeg-d3d11va + our existing keyed-mutex presenter).

---

## 8. Slice-1 status — 2026-11-24 (render primitives landed, unwired)

First slice of this port landed under `tools/video_editor/render/` — new
files only, **not yet wired** into `compositor.cc`/`monitor_surface.cc`/
`ve_app.cc` (owned by follow-up tickets). A self-test runs via
`video_editor --probe-render` (17/17 checks pass):

| File | Ticket | Contents |
|------|--------|----------|
| `render/video_frame.{h,cc}` | R3 (CPU half) / R9 | Refcounted, immutable `VideoFrame` — Chromium-style `StorageType` (`kOwnedMemory` now, `kTexture` slot reserved for D3D11/NV12), `coded_size`/`visible_rect`/`timestamp`, `WrapOwnedMemory`/`WrapTexture` factories, mailbox-style release callback fired on last ref. `VideoFramePool` is bounded and keyed by `size+format` — the zero-copy handoff primitive that replaces today's `assign()`/`make_shared<vector>` copies (B and C in §2) |
| `render/frame_scheduler.{h,cc}` | R7 / R8 | `TimeSource` interface + `AudioClockTimeSource` (front timestamp = `frames_written/sample_rate`, `SetBaseTime` rebase — same rule Chromium's `AudioRendererImpl` uses) + `WallClockTimeSource` (anchor + `wallDelta·rate`, freeze on pause — matches today's PreviewEngine math). `FrameScheduler` runs a BeginFrame-style tick on a vsync grid producing `(deadline, drop_budget)` and counts dropped/missed-deadline frames |
| `render/presenter.h`, `render/d2d_presenter.cc` | R6 seam | `Presenter` interface (`PresentVideoFrame(frame)` + `Composite(canvas)`); `D2DVideoPresenter` skeleton documenting where ANGLE SharedImage / keyed-mutex handoff plugs in |

**Findings while landing (affect R3–R9 execution):**

- `src/ui/rendering/windows/d2d_presenter.{h,cc}` already implements the
  Chromium SharedImage pattern: dedicated present thread owning the
  swapchain + second D3D11 device, keyed-mutex shared-texture handoff
  from the UI thread, coalesced newest-wins submit, dirty-rect
  `Present1`. The new `render/` presenter is a separate skeleton — slice
  2 should bridge into it rather than grow a parallel presenter.
- `src/media/base/audio_clock.h` + `src/media/renderers/time_source.h`
  already contain design-doc stubs in `XER::Media` — consider whether
  `render/frame_scheduler`'s TimeSource should satisfy/subclass that
  interface rather than duplicate it.
- `src/video/frame.h` `video::Frame` is mutable vector-backed and NOT
  refcounted; `render::VideoFrame` is a deliberately parallel type.
  Slice 2 needs a coexistence/adaptation story (adapt `video::Frame`
  internals or add `VideoFrame → video::Frame` view) before R9's
  `MonitorSurface::SetFrame(shared_ptr<const Frame>)` overload.
- Probe note: `VideoFramePool::idle_buffers` stays 0 while a re-acquired
  storage is still held by the caller — count only reflects truly-idle
  buffers (assertion in probe was corrected to match).

**Slice-2 integration points:** `OnVideoFrame` → `shared_ptr` handoff
through `preview_binding.cc` pending slots (R9); `MonitorSurface` pull
via `(t_min,t_max)` `FrameProvider` signature (R6 contract); feed
`FrameScheduler` from `D2DPresenter`'s post-`Present1` ack for a real
BeginFrame.
