# XER Editor

[English](README_en.md) | [Русский](README_ru.md)

A native Windows audio/video editor with a visual node-graph editor
(NodeGraph), Lua-scripted UI (LSUI), modular DSP/AI audio processing,
a full media pipeline (`src/media`), a Chromium-browser/VST3 host demo,
a full network stack, and FFmpeg integration.

- **Language:** C++23, Win32 API, Direct2D/Direct3D12, Skia (Ganesh), Lua 5.x
- **Build:** CMake 3.21+ + Ninja; Windows preset `windows-clang-msys2`
  (MSYS2 clang64 toolchain), alternative vcpkg preset for MSVC
- **Output binaries:** `out/build/windows-clang-msys2/bin/Editor.exe`,
  `XER_UI_Tests.exe`, `XER_Media_Smoke.exe`, `lsui.exe`,
  `EngineScanVST3.exe`, `XER_VST3_CEF.exe`
- **Runtime layout:** executables + `YA.*.dll` shared modules; all MSYS2
  dependencies are linked statically — no MinGW runtime DLLs need to be
  shipped (ggml/whisper/shaderc/vulkan are delay-loaded)

---

## Contents

- [Build and run](#build-and-run)
- [Architecture](#architecture)
- [Module map of `src/`](#module-map-of-src)
- [Node graph: model, executor, history](#node_graph)
- [UI: canvas, widgets, NodeGraph editor](#ui)
- [Flux: core, terminal, renderer](#flux)
- [LSUI — Lua UI](#lsui)
- [Audio: DSP, synth, plugins, codecs](#audio)
- [Network: sockets, HTTP, QUIC, WebSocket, S3](#network)
- [Video: FFmpeg decoder, sessions, audio player](#video)
- [SQL: ORM, query builder, SQLite](#sql)
- [AsyncIO: threads, memory, hardware abstraction](#async_io)
- [Cache, Helper, Wrappers](#misc)
- [Code examples](#code-examples)
- [Node.js addon](#node-addon)

---

## Build and run

### Windows (MSYS2 clang — primary preset)

```bat
:: Requires MSYS2 clang64 toolchain at C:\tools\msys64\clang64
cmake --preset windows-clang-msys2
cmake --build --preset windows-clang-msys2
```

Produces `out/build/windows-clang-msys2/bin/` — executables that import
only `YA.*.dll` (project modules) plus Windows system DLLs. All MSYS2
libraries (ICU, OpenSSL, curl, FFmpeg, sqlite, zstd, brotli, …) are
embedded statically via `xer_retarget_imported_to_static` in the root
`CMakeLists.txt`; the four DLL-only dependencies (ggml, whisper,
shaderc, vulkan) are delay-loaded by `YA.MediaDecoderUniversal.dll`.

### Windows (MSVC + vcpkg)

```bat
:: Requires Visual Studio 2022 + vcpkg (C:\vcpkg)
cmake -S . -B build2 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build2 --config Release
```

### Linux / macOS

Dependencies are a hybrid: system packages (apt / Homebrew) for everything
available in OS repositories, and partial vcpkg only for ports without a
system equivalent (skia, ggml, folly, onnxruntime, ...). The name mapping
lives in `cmake/deps/`, toolchain files in `cmake/platforms/`.

```bash
# Linux (Ubuntu 24.04): build the package list — the "Install build tools +
# system dependencies (apt)" step in .github/workflows/build.yml, then:
cmake --preset linux-release
cmake --build --preset linux-release

# macOS:
cmake --preset macos-arm64   # Apple Silicon
cmake --preset macos-x64     # Intel
cmake --build --preset macos-arm64
```

For partial vcpkg pass `-DXER_VCPKG_ROOT=<path-to-vcpkg> -DXER_VCPKG_TRIPLET=x64-linux` (or `arm64-osx`/`x64-osx`). cuDNN on Linux requires the CUDA toolkit (`cuda-toolkit-12-6` from NVIDIA's apt repository) — no GPU needed, only nvcc.

| Target | Binary | Purpose |
|---|---|---|
| `Editor` | `bin/Editor.exe` | Main application |
| `XER_UI_Tests` | `bin/XER_UI_Tests.exe` | UI/FFmpeg tests |
| `XER_Media_Smoke` | `bin/XER_Media_Smoke.exe` | Media pipeline + CSS engine smoke tests |
| `lsui` | `bin/lsui.exe` | Lua UI runtime |
| `EngineScanVST3` | `bin/EngineScanVST3.exe` | VST3 plugin scanner / probe (`--probe`, `--jobs N`) |
| `XER_VST3_CEF` | `bin/XER_VST3_CEF.exe` | Demo: VST3 editor + CEF browser + audio chain |

```bat
bin\Editor.exe                   :: editor (Projects → NodeGraph)
bin\lsui.exe script.lua          :: Lua script with UI windows
bin\XER_UI_Tests.exe             :: UI tests
bin\XER_Media_Smoke.exe          :: media + CSS smoke tests (21 checks)
bin\EngineScanVST3.exe           :: VST3 scan (async, --jobs cores)
bin\XER_VST3_CEF.exe             :: VST3 + CEF demo
```

Cache/logs/crash dumps: `%TEMP%\XER\cache\` (crash reports — `cache\crashes\`).

---

## Architecture

```
main.cc
 └─ src/app            Application shell: AppUI, windows, crash handler,
                       app NodeGraph nodes (VST3/Lua/FX/S3/Network)
 └─ src/ui             UI framework: Window, Canvas (HWND+Skia/D3D12),
                       theme, layout, 80+ widgets, NodeGraphEditorWidget
 └─ src/node_graph     Graph model: Graph/Node/Link/Group, History,
                       Executor (parallel, incremental, step-debug),
                       JSON serialization, type registry, ProjectStore (SQLite)
 └─ src/flux           Mini-framework: core, terminal emulator,
                       renderer, input, history — used by LSUI
 └─ src/lsui           lsui.exe — Lua scripts with UI windows,
                       watch(), Node.js-style require
 └─ src/audio          DSP engine: biquad/IIR/FIR, FFT (MKL), synth,
                       VST3/CLAP/AU hosts, effects, MIDI, codecs
 └─ src/content        Content modules: network/* (full network stack),
                       video (FFmpeg), s3, hex, clipboard, audio player
 └─ src/wrapper        Bridges to external libraries: Skia, FFmpeg, CUDA,
                       cuDNN, ANGLE, OpenCV
 └─ src/sql            SQL engine: SQLite storage, ORM, query builder
 └─ src/async_io       Async I/O: thread pool, buffers, prefetch,
                       hardware abstraction (CPU/GPU/thermals/NVML)
 └─ src/cache          Cache storage: persistent + temporary, scan/lock
 └─ src/helper         Utilities: string_utils, buffer, dict, compression,
                       diff, SpanReader/SpanWriter, raw_ptr
 └─ src/lunca          Minimal packet/udp_socket protocol (Lua network)
 └─ src/media          Media pipeline: demuxers (file/URL/buffered/MSE),
                       decoders, renderers, sinks, muxers, player, router,
                       capture, session — packaged into YA.*.dll modules
 └─ src/cef            CEF browser host (embedded Chromium views)
 └─ demo/              XER_VST3_CEF demo — VST3 editor embedding +
                       CEF view + audio loading/WASAPI/VST3 chain
 └─ tools/             Standalone utilities: EngineScanVST3,
                       XERVirtualCable, audio/video CLIs
```

All namespaces are unified under the `XER::` root
(`XER::Engine::*`, `XER::Content::*`, `XER::UI::*`, `XER::NodeGraph`, …).

---

## Module map of `src/`

### `src/app/` — application entry point

| File | Description |
|---|---|
| `app_main.cc/.h` | `App::Main` — Flux/UI initialization, event loop startup |
| `app_ui.cc/.h` | `AppUI` — window composition: Projects browser ↔ NodeGraph editor ↔ Settings ↔ Video; `pending_action_` for deferred transitions |
| `app_audio_controller.cc/.h` | `AudioController` — audio engine control from the UI |
| `app_audio_processor.cc/.h` | Audio stream processing (effects chain) |
| `app_node_graph_manager.cc/.h` | NodeGraph project manager |
| `node_graph_app_nodes.cc/.h` | App NodeGraph type registration: `plugin.vst3`, `plugin.vst3_view`, `lua.*`, `fx.*`, `net.*`, `s3.*`, `io.*` — eval functions, VST3/Lua state management |
| `crash_handler.cc/.h` | `App::Crash` — unhandled exception filter: minidump + crash.log + state.json + stackscan + auto-restart (max depth 2), `SetSessionProject`, `ConsumePendingReport` |

### `src/async_io/` — async I/O and hardware

| File | Description |
|---|---|
| `io_thread_pool.*` | Shared worker pool (`GetSharedInstance`) |
| `async_task_queue.*` | Deferred task queue |
| `async_file_reader/writer.*` | File read/write via the pool |
| `io_stream_reader/writer.*` | Streaming wrappers over files |
| `io_buffer_pool.*`, `async_buffer_pool.*` | Buffer pools (zero-alloc hot path) |
| `io_cache_manager.*` | Read-data caching |
| `io_file_map.*` | Memory-mapped files |
| `io_prefetch_manager.*` | Read-ahead prefetching |
| `io_priority_queue.*` | Priority task queue |
| `dnn_backends/cudnn_*` | cuDNN engines: bridge, convolution, pooling, activation |
| `hardware_abstraction/*` | CPU/GPU/Memory/Disk/Network/Battery info, `HwPowerMonitor` (RAPL), `HwTempSensor` + alert callback, `NvmlLoader` (NVIDIA NVML), `SystemInfoProvider` (async snapshot → JSON), `HwCapabilityScanner`, `HwTopologyMap`, `HwDirectIO` |
| `log_and_debug/*` | `IoPerfCounter`, `IoMemoryTracker`, `IoGpuUsageLogger`, `IoDumpHelper` |
| `sync_primitives/*` | `MutexWrapper`, `ReadWriteLock`, `Barrier`/`SyncBarrier`, `ConditionVariableWrapper`, `SyncEventListener`, `SyncSemaphoreCpu` |

### `src/audio/` — audio subsystem

#### `audio_core/` — real-time engine

`AudioEngine`, `AudioEngineHost`, `AudioAsyncManager` (offload to the pool),
`AudioBufferManager`, `AudioFifoQueue`, `AudioClockSync`, `AudioLatencyMonitor`,
`AudioInterleaveProcessor`, `AudioBitDepthConverter`, `AudioSampleRateConverter`,
`AudioSourceLoader`, `AudioThreadPriority`, `AudioCacheManager`, `AudioLogger`.

#### `dsp_algorithms/` — DSP primitives

`DspBiquadProcessor` (biquad filters), `DspIirFilterBank`, `DspFirFilterBank`,
`DspConvolutionEngine` (convolution), `DspFftEngine` (**MKL DFTI**),
`DspStftProcessor`, `DspPhaseVocoder`, `DspPitchShifterGranular`,
`DspSpectralShaper`, `DspDelayLineCircular`, `DspWindowingFunctions`.

#### `effects_rack/` — 66 files: effect primitives and chains

- **Dynamics:** `fx_dynamics_compressor/expander/gate/limiter`
- **EQ:** `fx_eq_graphic`, `fx_eq_parametric`, `custom_effect_eq_engine`, `custom_effect_eq_lite`
- **Modulation:** `fx_mod_chorus`, `fx_mod_flanger`, `fx_mod_phaser`, `fx_mod_extra`
- **Distortion:** `fx_distortion_tube`, `fx_distortion_bitcrush`
- **Reverb:** `fx_reverb_algorithmic`, `fx_reverb_convolution`
- **Pitch:** `fx_pitch_auto_tune_correct`
- **Custom FX framework:** `custom_effect*` (core, flow, invoke, node,
  node_wrapper — CLAP node wrapper, package, processor, algorithms,
  apply_multi, fxdata, struct, list, curve_line, accel, advanced_processing)

#### `plugin_wrappers/` — external plugins

`Vst3ExternalHost`/`Vst3HostInterface`/`Vst3PluginScanner` (scanning
via `EngineScanVST3.exe` — one `--probe` child process per module,
`std::async` bounded by CPU core count), `ClapExternalPluginHost`/
`ClapHostInterface`/`ClapMinimal`, `AuHostInterface` (macOS stub),
`PluginBuiltinHost`, `PluginParameterBridge`.

#### `audio_core/` — virtual audio cable

`VirtualAudioCable` — named cross-process PCM pipes + WASAPI bridges:
`cable_fx_chain` (built-in effects + VST3 stages per cable),
`cable_wasapi` (device bridge), `live_audio_output`,
`process_audio_capture` (per-process capture). CLI: `tools/XERVirtualCable`.

#### `synth_engine/` — synthesizer

`SynthOscAnalog/Fm/Noise/Wavetable`, `SynthEnvAdsr`, `SynthEnvMultistage`,
`SynthLfoStandard`, `SynthFilterLadder`, `SynthModulationMatrix`,
`SynthPolyphonyController`, `SynthVoiceManager`.

#### `file_io_codecs/` — codecs

`CodecWavPcm`, `CodecWavFloat`, `CodecFlacDecoder`, `CodecMp3Lame`,
`CodecOggVorbis`, `CodecAacAdts`, `CodecMetadataId3`,
`CodecFfmpegDecodeHelper`/`CodecFfmpegEncodeHelper`, `CodecFileStreamer`.

#### `analysis_ai/` — analysis and AI

`AudioFftAnalyzer`, `AudioPitchDetector`, `AnalPitchEstimator`,
`AnalSpectrogramGenerator`, `AnalBeatTracker`, `AnalOnsetDetection`,
`AnalLoudnessMeterLufs`, `AnalRmsPeakDetector`, `AnalHarmonicContent`,
`AiOpenvinoMusicRemark` (OpenVINO inference).

#### `midi_sequencing/`

`MidiParser`, `MidiEventDispatcher`, `MidiClockGenerator`,
`MidiPatternSequencer`, `MidiAutomationCurve`, `MidiControllerMapping`,
`MidiSysexHandler`.

#### `fx_customs/` — presets and ready-made chains

`FxCustomsEffectBuilder`, `FxCustomsPresets`, `FxCustomsMasteringEffects`,
`FxCustomsSpatial(Effects)`, `FxCustomsVintageEffects`, `FxCustomsVocalEffects`,
`FxCustomsEffectUtils`.

#### `demo/` — CLI demos and smoke tests

`AudioAnalysisCli`, `AudioDspDemo`, `AudioAnalysisSmoke`, `AudioModulesSmoke`,
`AudioFxCustom`.

### `src/node_graph/` — graph model

| File | Description |
|---|---|
| `node_graph.h/.cc` | `Graph`, `Node`, `PortRef`, `Link`, `Group`, `Value`, `NodeTypeDescriptor` — DAG model, validation (cycles, port types, dangling links), dirty-tracking, topological order / execution levels |
| `node_graph_value.*` | `Value` — variant: none/number/bool/string/array |
| `node_graph_registry.*` | `NodeTypeRegistry` — node type registration without touching the core |
| `node_graph_executor.*` | `Executor` — parallel level-based evaluation, incremental cache, external inputs/outputs, **step-debug**: `StepOnce(StepInfo)`/`ResetStep()`/`step_result()` |
| `node_graph_history.*` | `History` + `Command` (Apply/Revert/MergeWith), `MacroCommand`, `SnapshotCommand`, `GroupCommand` — undo/redo of all edits including groups |
| `node_graph_serializer.*` | JSON graph serialization, `kGraphFormatVersion`, load-time validation |
| `node_graph_json.*` | JSON DOM (`Json::Value`-like) |
| `node_graph_builtins.*` | Built-in types: math, string, array (convolve via MKL FFT), io.*, graph.input/output/subgraph |
| `node_graph_ext.*` | Extended types: MKL-accelerated array.* (vdSin, cblas), string.* (UTF-8, regex, base64, crc32, compress) |
| `node_graph_project_store.*` | `ProjectStore` — SQLite-backed projects: create/config/rename/delete, graphs, presets |

### `src/ui/` — UI framework

#### `core/`

`Window` (HWND + message loop, `frame_callback_` timer),
`Application` (singleton, Run/Shutdown).

#### `canvas/`

`Canvas` — child HWND over the window: raster via Skia surface **or**
GPU path D3D12+Ganesh (`TryInitGpu`, `GaneshD3DAllocator` committed
resources, flip-model swapchain, device-removed → raster fallback).
Input/render callbacks, clip stack, `DrawImageBgra` persistent texture.

#### `widgets/` — 80 widgets

- **NodeGraph:** `node_graph_editor_widget.*` — the main editor:
  pan/zoom-to-cursor, grid snap, node/link drag, box-select,
  copy/paste/duplicate, align/distribute, palette+search, context menus
  (Add Node, Problems, Group/Ungroup, Collapse/Expand, Step Graph,
  Reset Debug), subgraphs, minimap, validation badges, autosave,
  inspector with port values after run.
  `node_param_host.*` — transparent HWND overlay with native parameter
  controls inside nodes (EDIT/COMBOBOX/CHECKBOX/file browse),
  click-through via `WS_EX_LAYERED`+colorkey.
- **Basic:** `widget`, `widget_tree`, `button`, `label`, `checkbox`,
  `radio_group`, `select`, `switch`, `toggle`, `slider`, `text_input`,
  `textarea`, `badge`, `card`, `alert`, `dialog`, `context_menu`,
  `breadcrumb`, `pagination`, `progress`, `separator`, `skeleton`,
  `spinner`, `tabs`, `tooltip`, `scroll_area`, `accordion`, `avatar`
- **Specialized:** `projects_widget` (Project browser:
  Select/Config/Duplicate/Delete + search), `settings_widget`,
  `fx_rack_widget` + `fx_rack_state` (effects chain),
  `eq_editor` (EQ curves), `waveform_widget`, `audio_player_widget`,
  `video_preview_widget`, `chain_map_widget`, `screen_loading_widget`
  (splash: DB/projects/cache/driver checks)
- **Kit primitives:** `kbd`, `marker`, `aspect_ratio`, `button_group`,
  `input_group`, `field`, `item`, `empty`, `collapsible`, `direction`,
  `form`, `attachment`, `bubble`, `message`, `message_scroller`,
  `input_otp`, `hover_card`, `popover`, `dropdown_menu`, `sheet`,
  `drawer`, `resizable`, `carousel`

#### `theme/`, `layout/`, `motion/`, `colorspace/`, `rendering/`, `platform/`, `backends/`

- `theme/` — design-token light/dark theme (`Theme::Tokens`, `Theme::Color`)
- `layout/geometry.h` — Rect/Point/Size
- `motion/` — `Motion` (easing animations), `accel` (**MKL VML** for float ops)
- `colorspace/` — `ColorSpace`, `ColorManager`, `HdrManager`
- `rendering/renderer.h` — renderer interface
- `rendering/windows/direct2d_renderer.*` — D2D backend
- `platform/windows/input_translator.*` — Win32 input → events
- `debug/` — DevTools: `debug_map_ui.*` (widget tree → XML via libxml2),
  `elements_view.*` (collapsible tree panel), `dev_tools.*` (inspector
  window: Elements / Render / Stats tabs)
- `xml/xml_ui.*` — declarative widget trees from XML markup (libxml2)
- `backends/flux/` — Flux adapters: `FluxRenderer`, `FluxInputAdapter`
- `demo/audio_player_demo.cc` — demo

#### `css/` — CSS engine (`XER::UI::CSS`, → `YA.DesginCSS.dll`)

Chromium-style style calculation and invalidation:

- `css_selector.*` — parser + matcher: tag, `.class`, `#id`, `*`,
  `[attr]`, `[attr=value]`, `:focus`, `:hover`, `:active`,
  `:first/last-child`, `:nth-child(an+b)`; descendant and `>`
  combinators; comma-separated selector lists
- `rule_set.*` — indexed `RuleData` storage (`std::deque` — pointer
  stability): ClassRules/IdRules/TagRules/Universal maps keyed by the
  right-most compound; ancestor-class → `InvalidationSet` index
- `selector_checker.*`, `element_rule_collector.*` — matching
- `style_resolver.*`, `computed_style.h` — cascade:
  UA < user < author → specificity → source order
- `invalidation_set.*`, `pending_invalidations.h`,
  `style_invalidator.*` — pending invalidation map, DFS invalidation,
  self/child recalc flags
- `css_node.h`, `tree_scope.h`, `document_style.*` — DOM adapter
  (`CSSElement`/`CSSNode` state bits) and
  `UpdateStyleAndLayoutTree` — push pending → recalc in document order
- `stylesheet_parser.*` — text CSS → `StyleSheet`/`StyleRule`

### `src/media/` — media pipeline (`XER::Media::*`)

Modular playback/processing stack partitioned into `YA.*.dll` modules:

|| Submodule | Purpose |
||---|---|
|| `base/` | `SeekableBuffer`, media log, time utilities |
|| `demuxer/` | `DataSource` interface, `FileDataSource`,
  `UrlDataSource` (HTTP range requests via the network layer),
  `BufferedDataSource` (byte-window read-ahead cache),
  `MediaSource`/`ChunkDemuxer` (MSE-style append/EOS),
  FFmpeg demuxer bridge |
|| `decoders/` | Audio/video decoder interfaces + FFmpeg adapters
  (universal decoder lives in `YA.MediaDecoderUniversal.dll`) |
|| `renderers/` | `AudioRendererImpl`, `VideoRendererImpl`,
  audio-clock sync, frame compositor |
|| `sinks/` | `WasapiOutputStream`, `FrameCacheSink`,
  device-thread pull model |
|| `muxers/` | Container muxers (EBML/MP4 family) |
|| `player/` | `MediaPlayer`/`MediaPlayerImpl` — pipeline orchestration |
|| `session/`, `service/` | Media session and out-of-process service |
|| `router/` | Media routing between sources, renderers, and sinks |
|| `gpu/`, `capture/`, `cdm/`, `filters/`, `metrics/` | GPU interop,
  capture devices, CDM stubs, filter graph, metrics |

### `src/flux/` — minimal framework (LSUI core)

| Submodule | Files | Purpose |
|---|---|---|
| `core/` | `flux_core`, `flux_config`, `flux_context`, `logger`, `module_manager` | Init, config, logging, modules |
| `terminal/` | `terminal_emulator`, `terminal_buffer`, `terminal_ansi_parser`, `terminal_colors`, `terminal_styles`, `terminal_extended`, `terminal_input_handler`, `terminal_interface`, `terminal_manager`, `terminal_output_renderer`, `terminal_size_detector`, `terminal_window` | Full ANSI terminal: buffer, parser, styles, fit/center/rule, snapshot |
| `rendering/` | `renderer`, `render_context`, `render_pipeline`, `frame_buffer` | Render pipeline |
| `input/` | `input_manager`, `key_binding_manager`, `command_parser` | Input and bindings |
| `history/` | `action_history`, `command_history` | Action/command history |

### `src/lsui/` — lsui.exe: Lua UI

| File | Description |
|---|---|
| `lsui.cc/.h` | `UiWindow` — Canvas-based window, Lua API: `ui.window{}`, `on_tick/on_mouse/on_key`, immediate-mode draw (`rect/text/button`...), `watch()` in pure Lua, crash filter in main |
| `lsui_term.cc` | `term.*` module over `flux/terminal`: print/println/write/write_err, clear/snapshot/text, size, fit/center/rule, fg/bg/style/reset |
| `main.cc` | Entry point: Lua state, `kLuaHelpers` — **Node.js-style require**: `./rel`, `../rel`, `node_modules` walk-up, `LSUI_PATH`, `mod.lua`/`mod/init.lua`, `module.exports`/`exports`, circular-require partial exports, `require.resolve`, `__dirname`/`__filename`/`module`/`exports` |

### `src/wrapper/` — bridges to external libraries (`engine::bridge::*`)

| Module | Files | Purpose |
|---|---|---|
| `skia/` | `skia_engine_bridge` + `_codec_extra`, `_fx`, `_image_filter`, `_lottie`, `_m44`, `_pdf`, `_shader`, `_svg` | Skia API: image codecs, color filters, shaders, SVG/Lottie/PDF, matrices |
| `ffmpeg/` | `ffmpeg_engine_bridge`, `ffmpeg_video_decoder` | FFmpeg: decoder with audio-tap (`EnableAudioTap`, `ReadAudioFrames` on a separate thread, packet queue under mutex) |
| `cuda/` | `cuda_engine_bridge` | CUDA runtime bridge |
| `cudnn/` | `cudnn_engine_bridge` | cuDNN bridge |
| `angle/` | `angle_engine_bridge` | ANGLE (OpenGL ES → D3D) |
| `opencv/` | `opencv_engine_bridge` | OpenCV bridge |

### `src/content/` — content modules (`XER::Content::*`)

#### `network/` — network stack (~100 files, namespace `XER::network::*`)

| Submodule | Files | Purpose |
|---|---|---|
| `socket/` | `tcp_client_socket`, `udp_client_socket`, `ssl_client_socket`, `socket_client`, `socket_server`, `socket_stream`, `socket_proxy`, `network_selector`, `net_log` + `platform/{windows,posix}/socket_platform_*` | Sockets: TCP/UDP/SSL, selector, proxy, platform abstractions |
| `ssl/` | `ssl_context`, `ssl_session_cache` | SSL context and session cache |
| `cert/` | `cert_database`, `cert_storage`, `cert_transparency`, `cert_verifier`, `cert_verify_result`, `nss_certificate`, `scoped_nss_types` | X.509/NSS certificates, CT logs, verification |
| `dns/` | `dns_client`, `dns_resolver`, `dns_config_service`, `dns_record_type`, `host_resolver`, `host_resolver_impl` | DNS resolver |
| `http/` | `http_auth`, `http_cache`, `http_network_delegate`, `http_network_session`, `http_request/response_headers`, `http_server_properties`, `http_stream`, `http_stream_factory`, `http_sync_request`, `http_transaction`, `http_util` | HTTP/1.1 client: sessions, transactions, cache, auth |
| `quic/` | `quic_connection`, `quic_crypto_stream`, `quic_network_delegate/session`, `quic_packet`, `quic_protocol`, `quic_server_id`, `quic_stream`, `quic_version_manager` | QUIC protocol |
| `spdy/` | `spdy_framer`, `spdy_network_delegate`, `spdy_protocol`, `spdy_session`, `spdy_stream` | SPDY protocol |
| `websocket/` | `websocket_frame`, `websocket_handshake_stream`, `websocket_network_delegate`, `websocket_stream`, `websocket_event_interface` | WebSocket client (RFC6455) |
| `ftp/` | `ftp_ftp_request`, `ftp_ftp_stream`, `ftp_ftp_network_delegate` | FTP client |
| `url/` | `url_request`, `url_request_context(+getter)`, `url_request_intercepting_job`, `url_request_job`, `url_request_util`, `url_util` | URL requests and parsing |
| `proxy/` | `proxy_config`, `proxy_info`, `proxy_resolver(+factory)`, `proxy_service`, `socks_client_socket` | HTTP/SOCKS proxy |
| `transport/` | `transport_client_socket`, `transport_server_socket`, `transport_io_buffer`, `transport_network_delegate/session` | Transport layer |
| `core/` | `network_context`, `network_service`, `network_service_delegate`, `network_service_util`, `network_task_runner` | Network service and context |
| `async/` | `async_operation_manager` + `iocp/iocp_completion_port`, `posix/epoll_async_socket` | Async operations: IOCP (Windows) / epoll (POSIX) |
| `cache/` | `network_cache_manager` | HTTP response cache |
| `logging/` | `network_logger` | Network logging |
| `network_util/` | `network_util` | Utilities |

#### Other content modules

| Module | Files | Purpose |
|---|---|---|
| `video/` | `video_session` (background decode worker), `video_decoder`, `video_engine`, `video_audio_player` (WASAPI streaming from shared decoder), `video_frame(+cache)`, `video_info`, `video_command_buffer` | Video: FFmpeg decoder, play/pause/seek, audio track from the same container |
| `s3/` | `s3_client`, `s3_signer` (AWS Signature V4), `s3_types` | S3-compatible storage |
| `hex/` | `hex.h` (+`detail`) | HEX encode/decode of bytes |
| `clipboard/` | `clipboard` | Clipboard |
| `audio/` | `audio_player` | Content audio player |

### `src/sql/` — SQL engine (`XER::Engine::SQL::*`)

| Submodule | Files | Purpose |
|---|---|---|
| `storage/` | `sqlite_database` | SQLite wrapper: open/exec/prepare/transaction |
| `modern/` | `query_builder`, `query` | Fluent query builder (`SELECT/INSERT/UPDATE/DELETE`) |
| `orm/` | `table_schema` | Table schemas for ORM |
| `content/` | `value`, `row` | `Value` (SQL types), `Row` |
| `plus/` | `transaction` | Transactions |

### `src/cache/` — cache storage (`XER::Engine::Cache`)

`CacheManager` (singleton, `Persistent()`/`Temporary()` roots),
`PersistentStorage`, `TemporaryStorage`, `CacheStorage`, `CacheEntry`,
`CacheConfiguration`, `CacheConstants`, `CacheEncryptionHandler`,
`CacheScanAndLock`, `CacheUtils`.

### `src/helper/` — utilities (`XER::Engine::Helper`)

`string_utils` + `helper/string` (String helpers, UTF-8),
`buffer` (Buffer), `dict` (Dict), `file_diff` (diff),
`compression_codec` (zlib), `jsoncpp_helper` (Json::Value bridge),
`tool_to` (Tool::ToString etc.).

### `src/lunca/` — Lua network (`common/`)

`packet` (packet packing), `udp_socket` — minimal UDP for Lua.

### Root files

| File | Description |
|---|---|
| `main.cc` | Entry point: crash-handler install → App::Main |
| `app_command.h/.cc` | `XER::AppCommand` — CLI command parser (in `namespace XER`) |
| `engine_params.h/.cc` | `XER::EngineParams` — runtime config from CLI/env |
| `audio_config_helper.h/.cc` | Applying audio-JSON config to commands |
| `json_config_helper.h/.cc` | JSON config helpers |
| `tools/EngineScanVST3/main.cc` | VST3 plugin scanner utility (path, name, parameters) |

---

## `<a id="node_graph"></a>`NodeGraph: model, executor, history

### Model (`Graph`)

```cpp
#include "node_graph/node_graph.h"

XER::NodeGraph::Graph graph(registry);

auto& math = graph.AddNode("math.add");       // NodeId
graph.MoveNode(math.id, 100.0, 50.0);

// Ports and links
graph.Connect({src_node, "out"}, {dst_node, "in"});

// Groups (visual frames)
XER::NodeGraph::Group grp;
grp.title = "DSP chain"; grp.x = 0; grp.y = 0; grp.w = 400; grp.h = 300;
grp.color = "#6366f1";
graph.AddGroup(grp);

// Validation
for (const auto& issue : graph.Validate()) {
    // issue.severity / issue.message / issue.node
}
```

### Executor — run, incremental, step-debug

```cpp
XER::NodeGraph::Executor ex(graph);

// Full run
XER::NodeGraph::ExecutionResult res = ex.Run();
// res.ok / res.evaluated / res.cached / res.node_errors / res.logs

// Step execution (one execution level per call)
XER::NodeGraph::Executor::StepInfo step;
while (ex.StepOnce(&step)) {
    // step.level, step.total_levels, step.nodes — this step's nodes
    // port values are already in the cache:
    auto v = ex.GetOutput(step.nodes.front(), "out");
}
ex.ResetStep();          // reset the session
ex.InvalidateCache();    // full invalidation
```

### History — undo/redo

```cpp
XER::NodeGraph::History hist(graph);
hist.BeginMacro("Move nodes");
hist.MoveNode(id, x, y);
hist.AddGroup(std::move(grp));
hist.EndMacro();
hist.Undo();   // rolls back the whole macro
hist.Redo();
```

### Serialization and projects

```cpp
#include "node_graph/node_graph_serializer.h"
#include "node_graph/node_graph_project_store.h"

std::string json = XER::NodeGraph::SerializeGraph(graph);
XER::NodeGraph::Graph loaded(registry);
XER::NodeGraph::DeserializeGraph(json, &loaded);   // version check

// SQLite-backed projects
XER::NodeGraph::ProjectStore store(db_path);
store.CreateProject("My Project", &err);
store.SaveGraph(project_id, "main", graph);
```

---

## `<a id="ui"></a>`UI: canvas, widgets, NodeGraph editor

### Canvas — HWND + Skia/D3D12

```cpp
XER::UI::Canvas canvas;
canvas.Create(parent_hwnd);
canvas.SetRenderCallback([](XER::UI::Canvas& c) {
    c.Clear({0.1f, 0.1f, 0.12f, 1.0f});
    c.DrawText("Hello", 10, 10, 14.0f, {1, 1, 1, 1});
});
canvas.SetInputCallback([](unsigned msg, int x, int y, int btn,
                          int wheel, int key, int mods) {
    return false;  // true = consumed
});
```

GPU path: D3D12 + Ganesh, own `GaneshD3DAllocator`
(committed resources), flip-model swapchain, raster
fallback on device-removed.

### NodeGraphEditorWidget

```cpp
auto editor = std::make_shared<XER::UI::Widgets::NodeGraphEditorWidget>(
    hwnd, theme_hook, project_id);
editor->SetHostVisible(true);          // canvas + param overlay
editor->SetPluginEditorHandler(...);   // VST3 editor HWND
editor->SetBrowserBackHook(...);       // "back" button
window->SetContent(editor);
```

Features: pan/zoom-to-cursor, grid snap, node/link drag,
box-select, multi-select, copy/paste/duplicate, align/distribute,
node search (palette), context menus, subgraphs, minimap,
groups (Group/Ungroup Selection), node collapse (header+ports),
validation badges + Problems menu, step-debug (Step Graph/Reset Debug),
autosave, inspector with port values.

### Widgets

All widgets are immediate-mode over `Renderer` + `WidgetTree`:
`Button`, `Label`, `Checkbox`, `Select`, `Slider`, `TextInput`,
`Dialog`, `ContextMenu`, `Tabs`, `ScrollArea`, `ProjectsWidget`,
`SettingsWidget`, `FxRackWidget`, `EqEditor`, `WaveformWidget`,
`AudioPlayerWidget`, `VideoPreviewWidget`, `NodeParamHost`, etc.

Theme — design tokens: `Theme::Tokens` (light/dark),
`Theme::Color{r,g,b,a}`, `Mix`, `AccentC`, `DangerC`, `WarnC`.

---

## `<a id="flux"></a>`Flux: core, terminal, renderer

Flux is the minimal framework LSUI (and partially the UI) is built on.

```cpp
#include "flux/core/flux_core.h"
#include "flux/terminal/terminal_emulator.h"

XER::flux::core::FluxCore::Instance().Initialize(config);
auto& term = XER::flux::terminal::TerminalEmulator::Instance();
term.Print("Hello");
term.Fg(XER::flux::terminal::Color::kBrightBlue);
term.Println("world");
```

- `flux/core/` — `FluxCore` (init/shutdown), `FluxConfig`, `FluxContext`, `Logger`, `ModuleManager`
- `flux/terminal/` — `TerminalEmulator` + `TerminalBuffer` + `TerminalAnsiParser` + styles/colors: `print/println/write/write_err`, `clear/snapshot/text`, `size()`, `fit/center/rule`, `fg/bg/style/reset`
- `flux/rendering/` — `Renderer`, `RenderContext`, `RenderPipeline`, `FrameBuffer`
- `flux/input/` — `InputManager`, `KeyBindingManager`, `CommandParser`
- `flux/history/` — `ActionHistory`, `CommandHistory`

---

## `<a id="lsui"></a>`LSUI — Lua UI

`lsui.exe script.lua` — runs a Lua script with UI windows.

```lua
-- demo.lua
local ui = require("lsui")

local win = ui.window({
    title = "My App",
    width = 400, height = 300,
    on_tick = function(w, dt) end,
})

win:button({ x = 10, y = 10, w = 100, h = 30, label = "Click" })

ui.watch(1.0, function()          -- every 1 second
    print("tick")
end)

-- Node.js-style require:
local util = require("./lib/util")        -- relative to file
local pkg  = require("mylib")             -- node_modules walk-up / LSUI_PATH
```

Terminal module:

```lua
local term = require("term")
term.println("Hello", { fg = "bright_blue", bold = true })
term.rule("=")
local w, h = term.size()
local snap = term.snapshot()
```

---

## `<a id="audio"></a>`Audio: DSP, synth, plugins, codecs

### DSP primitives (`audio/dsp_algorithms`)

```cpp
#include "audio/dsp_algorithms/dsp_biquad_processor.h"
#include "audio/dsp_algorithms/dsp_fft_engine.h"

XER::Engine::Audio::DSP::BiquadProcessor biquad;
biquad.SetLowpass(44100.0, 1000.0, 0.707);
biquad.Process(samples.data(), samples.size());

XER::Engine::Audio::DSP::FFTEngine fft(1024);   // MKL DFTI
fft.Forward(real_in, complex_out);
```

### VST3/CLAP hosting

```cpp
#include "audio/plugin_wrappers/vst3_external_host.h"
#include "audio/plugin_wrappers/clap_external_plugin_host.h"

XER::Engine::Audio::Plugin::Vst3ExternalHost host;
host.Load("C:/Plugins/MySynth.vst3", 44100.0, 512, &err);
host.SetParameter(0, 0.75f);
host.Process(in_block, out_block, 512);
```

### NodeGraph audio nodes (`app/node_graph_app_nodes`)

```cpp
// Type registration: plugin.vst3, plugin.vst3_view, lua.buffer,
// fx.<algo>, net.websocket_*, s3.*, io.file_*, audio.*
// VST3/Lua states live in g_vst3_hosts / g_lua_states under mutex.
```

---

## `<a id="network"></a>`Network: sockets, HTTP, QUIC, WebSocket, S3

```cpp
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/websocket/websocket_stream.h"
#include "content/network/http/http_sync_request.h"

// TCP
XER::Content::Network::TcpClientSocket sock;
sock.Connect("example.com", 80, &err);

// WebSocket (NodeGraph node net.websocket_send)
auto ws = std::make_unique<XER::network::websocket::WebSocketStream>(
    std::move(sock));
ws->SendText("hello", &err);

// HTTP sync
XER::network::http::HttpSyncRequest req;
auto resp = req.Fetch("https://api.example.com/data");
```

S3:

```cpp
#include "content/s3/s3_client.h"
XER::Content::S3::S3Client s3(endpoint, bucket, access_key, secret_key);
s3.PutObject("path/file.bin", data, &err);
```

---

## `<a id="video"></a>`Video: FFmpeg decoder, sessions, audio player

```cpp
#include "content/video/video_session.h"
#include "content/video/video_audio_player.h"

XER::Content::Video::VideoSession session;
session.Open("video.mp4", &err);     // background decode worker
session.Play(); session.Pause(); session.Seek(10.0);

XER::Content::Video::VideoAudioPlayer player;
player.Attach(session.GetAvDecoder());         // same container!
player.Play();
```

`VideoDecoder` — FFmpeg wrapper: video frames on one thread,
`ReadAudioFrames` on a second (audio packets via mutex queue),
`EnableAudioTap(true)` — audio from the same open container.

---

## `<a id="sql"></a>`SQL: ORM, query builder, SQLite

```cpp
#include "sql/storage/sqlite_database.h"
#include "sql/modern/query_builder.h"

XER::Engine::SQL::Storage::SqliteDatabase db;
db.Open("projects.db");

auto q = XER::Engine::SQL::Modern::QueryBuilder::Select("*")
    .From("projects").Where("id = ?", 42).Build();
auto rows = db.Query(q);
```

---

## `<a id="async_io"></a>`AsyncIO: threads, memory, hardware

```cpp
#include "async_io/io_thread_pool.h"
#include "async_io/hardware_abstraction/system_info.h"

auto& pool = XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();
pool.Enqueue([] { /* worker task */ });
pool.WaitAll();                      // wait for the queue

// System info (async snapshot → JSON)
auto fut = XER::AsyncIO::HardwareAbstraction::SystemInfoProvider::Instance()
    .GetSnapshotAsync();
```

`HwTempSensor` — temperature monitoring with alert callback;
`HwPowerMonitor` — RAPL power (Linux) / stub (Windows);
`NvmlLoader` — NVIDIA NVML.

---

## `<a id="misc"></a>`Cache, Helper, Wrappers

```cpp
#include "cache/cache_manager.h"
#include "helper/string_utils.h"

// Cache: %TEMP%/XER/cache — persistent + temporary roots
auto& cache = XER::Engine::Cache::CacheManager::Instance();
auto root = cache.Persistent().root();

// String utils
XER::Engine::Helper::String::ToUpper("hello");   // "HELLO"
XER::Engine::Helper::String::Split("a,b,c", ',');

// HEX
XER::Content::Hex::Encode(data.data(), data.size());
XER::Content::Hex::Decode("deadbeef");
```

Wrappers (`engine::bridge::*` → now `XER::engine::bridge::*`):
Skia (full graphics API + codecs + SVG/Lottie/PDF), FFmpeg,
CUDA, cuDNN, ANGLE, OpenCV — thin C++ wrappers over C APIs.

---

## `<a id="code-examples"></a>`Code examples

### 1. NodeGraph: create a graph, add nodes, connect, save

```cpp
#include "node_graph/node_graph.h"
#include "node_graph/node_graph_registry.h"
#include "node_graph/node_graph_builtins.h"
#include "node_graph/node_graph_serializer.h"
#include "node_graph/node_graph_executor.h"
#include "node_graph/node_graph_history.h"

using namespace XER::NodeGraph;

NodeTypeRegistry registry;
RegisterBuiltinNodes(registry);       // math/string/array/io/graph.*
RegisterExtendedNodes(registry);      // MKL-accelerated extensions

Graph graph(&registry);
History hist(graph);
Executor ex(graph);

auto& a = hist.AddNode("math.add", 0, 0);
auto& b = hist.AddNode("math.mul", 200, 0);
auto& out = hist.AddNode("io.print", 400, 0);

hist.Connect({a, "out"}, {out, "in"});
hist.Connect({b, "out"}, {out, "in2"});

// Step-debug
Executor::StepInfo step;
while (ex.StepOnce(&step)) {
    printf("level %zu: %zu nodes\n", step.level, step.nodes.size());
}

// Save
std::string json = SerializeGraph(graph);
```

### 2. NodeGraph: groups + collapse + validation

```cpp
// Group via History (undoable)
Group grp{.title = "Chain", .x = -20, .y = -30, .w = 500, .h = 200,
          .color = "#6366f1"};
hist.AddGroup(grp);

// Validation
for (const auto& issue : graph.Validate()) {
    if (issue.severity == ValidationIssue::Severity::kError) {
        printf("ERROR node %llu: %s\n",
               (unsigned long long)issue.node, issue.message.c_str());
    }
}

// Editor-side collapse: Level::collapsed_nodes — set<NodeId>
// A collapsed node = header + ports (no param rows), links still work.
```

### 3. LSUI: window + require + watch

```lua
-- main.lua
local ui = require("lsui")
local term = require("term")
local util = require("./lib/util")

term.println("LSUI demo", { fg = "cyan", bold = true })

local win = ui.window({ title = "Demo", width = 320, height = 200 })

win:button({ x = 10, y = 10, w = 120, h = 28, label = "OK",
             on_click = function() win:close() end })

ui.watch(0.5, function()
    term.print(".")
end)
```

```lua
-- lib/util.lua
local M = {}
function M.greet(name) return "Hello, " .. name end
return M
```

### 4. VST3 plugin in NodeGraph

```lua
-- In NodeGraph add a plugin.vst3 node, params:
--   path = "C:/Plugins/Serum.vst3"
--   sampleRate = 44100
--   p0..p7 = normalized params (0..1)
-- After the graph runs the editor opens the plugin GUI automatically.
```

### 5. FFmpeg video + audio

```cpp
XER::Content::Video::VideoSession vs;
vs.Open("movie.mkv");

// Audio from the same decoder:
XER::Content::Video::VideoAudioPlayer ap;
ap.AttachDecoder(vs.SharedDecoder());
ap.Play();
vs.Play();
// SyncTo() keeps the playheads in sync (drift > 300ms → seek).
```

### 6. AsyncIO thread pool + hardware info

```cpp
auto& pool = XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();
auto fut = pool.PostAndWait([] {
    return XER::AsyncIO::HardwareAbstraction::SystemInfoProvider::Instance()
        .Refresh().cpu_model;
});
printf("CPU: %s\n", fut.get().c_str());
```

### 7. Crash handler (already built into main.cc)

```cpp
// main.cc:
XER::App::Crash::Install();          // before everything else
// On crash: minidump + crash.log + state.json →
// %TEMP%/XER/cache/crashes/<timestamp>/, auto-restart (--crash-depth<=2)
// stackscan in crash.log shows return addresses without frame pointers.
```

---

## `<a id="node-addon"></a>`Node.js addon

`addon/` builds `xer.node` — a NAN/V8 native module exposing the XER API to
Node.js as ES classes (one wrapper class per file, grouped by subsystem).

```js
const xer = require('./addon/index.js');
const { audio, UI } = xer;
```

### Audio class API (`xer.audio`)

Top-level: `audio.info(path)`, `audio.convert(in, out, opts?)`,
`audio.Buffer` (decoded PCM — the only input `audio.Player` accepts),
`audio.Player` (WASAPI playback).

Subsystem namespaces — every native class is a JS class:

| Namespace | Classes / functions |
|---|---|
| `audio.dsp` | `Biquad`, `FFT`, `STFT`, `DelayLine`, `Convolution`, `PhaseVocoder`, `PitchShifter`, `SpectralShaper`, `FIRBank`, `IIRBank`; `Windowing` functions (`Hann`, `Hamming`, `Blackman`, `SqrtHann`, `Apply`, `Describe`) |
| `audio.fx` | `Compressor`, `Expander`, `Gate`, `Limiter`, `Bitcrush`, `Tube`, `GraphicEQ`, `ParametricEQ`, `Chorus`, `Tremolo`, `Vibrato`, `RingMod`, `Flanger`, `Phaser`, `AutoTune`, `ReverbAlgo`, `ReverbConv`; `audio.fx.custom` — `List()`, `RenderExample(rate, samples)`, `Run(pkg, rate, samples)`, `RunInterleaved(pkg, rate, samples, channels)` |
| `audio.analysis` | `BeatTracker`, `HarmonicContent`, `LoudnessMeter`, `OnsetDetector`, `PitchEstimator`, `RmsPeak`, `Spectrogram`, `FFTAnalyzer`, `PitchDetector` |
| `audio.codec` | `WavPcm`, `WavFloat`, `Flac`, `Mp3`, `Ogg`, `Aac`, `Id3`, `FileStreamer` |
| `audio.midi` | `Parser`, `Sequencer`, `Clock`, `Mapping`, `Dispatcher` (JS callbacks), `Sysex`, `Automation` |
| `audio.synth` | `Adsr`, `MultistageEnv`, `Filter`, `Lfo`, `ModMatrix`, `OscAnalog`, `OscFm`, `OscNoise`, `OscWavetable`, `Polyphony`, `VoiceManager` |
| `audio.core` | `Engine`, `EngineHost`, `ClockSync`, `Fifo`, `Interleave`, `BitDepth`, `Resample`, `NativeBuffer`, `Latency`, `SourceLoader`, `Logger`, `BufferManager`, `CacheManager`, `AsyncManager`; thread-priority functions (`SetCurrentThreadPriority`, `SetThreadAffinity`, `GetOptimalCore`, `GetPriorityName`, …) |
| `audio.plugin` | `ParamBridge`, `BuiltinHost`, `Vst3Host`, `ClapHost`, `AuHost`, `Vst3External`, `ClapExternal`; `ScanVst3Plugins(roots?)`, `DefaultVst3SearchRoots()` |

```js
const buf = audio.Buffer.FromFile('song.mp3');        // or new audio.Buffer({...})
const player = new audio.Player();
player.Load(buf);                                     // Buffer ONLY

const comp = new audio.fx.Compressor();
comp.SetThresholdDb(-20); comp.SetRatio(4);
const out = comp.ProcessBlock(pcm);                   // Float32Array -> Float32Array

const src = new audio.core.SourceLoader();
const decoded = src.Load({ path: 'in.wav', targetSampleRate: 48000 });

const pkg = { label: 'my-chain', nodes: [
  { label: 'comp', backend: 'builtin', algorithm: 'Compressor' }]};
const res = audio.fx.custom.Run(pkg, 44100, pcm);     // {output, report}
```

Conventions: audio buffers are `Float32Array` (interleaved for multi-channel),
byte streams `Uint8Array`. Methods taking `(samples, channels)` treat the
array as interleaved frames×channels; mono DSP/FX methods take the array
length as the frame count.

### Design UI (`xer.UI`)

`UI.Application`, `UI.Window`, `UI.Widget` subclasses plus ~60 themed widgets:
primitives (`Button`, `Label`, `TextInput`, `Slider`, …), overlays
(`Popover`, `DropdownMenu`, `HoverCard`, `Sheet`, `Drawer`, `Dialog`,
`ContextMenu`, `Command`), data display (`Table`, `Chart`, `Calendar`,
`Sidebar`), inputs (`Combobox`, `InputOTP`, `Select`, …).

#### DevTools (`src/ui/debug/`)

Two developer-facing inspectors built on the widget tree:

- **`win.OpenDevTools()`** (addon) / `Debug::DevTools::Inspect(window)`
  (C++) — opens a separate DevTools window with three tabs:
  **Elements** (collapsible widget tree with type, bounds and state,
  plus a detail pane showing the selected subtree), **Render** (live
  `GetRenderStats()` frame timing, damage coverage, last paint error)
  and **Stats** (widget counts by type, visibility/enabled totals,
  max depth). Auto-refreshes ~1 s; the window lives until closed.
- **`widget.DumpXml()`** (addon) / `Debug::DebugMapUI::Dump(widget)`
  (C++) — serializes the widget subtree to an XML document via libxml2:
  element per widget (`<Button type="XER::UI::Widgets::Button" …>`)
  with geometry (`x/y/w/h`), state (`visible/shown/enabled/focused/
  hovered/focusable/clips/dir`) and `text`/`tooltip` attributes
  (`Widget::GetDebugText()` supplies caption text for Label/Button).
  XML escaping and depth/cycle guards are built in.

#### XML UI markup (`src/ui/xml/`)

`Xml::Load` / `Xml::LoadFile` (C++) and `UI.parseXml` / `UI.parseXmlFile`
(addon) instantiate widget trees from XML — "HTML, but XML": element
names map to widget classes, attributes to properties, nested elements
to children:

```xml
<ScrollArea><Form columns="2" gap="8">
  <Label text="Name"/>
  <TextInput placeholder="Enter name"/>
  <Button text="Save" style="primary"/>
</Form></ScrollArea>
```

Common attributes on every element: `x`/`y`/`width`/`height` (DIP
bounds), `visible`, `enabled`, `tooltip`, `dir`. Containers interpret
children specially (`Tabs` takes `Tab`, `Table` takes `Column`/`Row`,
`ScrollArea` takes one content child, … — see `xml_ui.cc`).

The loader never throws: it returns `{ ok, widget, warnings, error }`.
Unknown elements are skipped with a `line N:` warning; an unknown or
malformed root is fatal (`ok === false`). Check `warnings` for typos —
`UI.xmlSupportedTags()` lists all ~50 supported tags for tooling.
Requires libxml2 (vcpkg `libxml2` port; without it the API returns a
descriptive `error` instead of a tree).

---

### OpenAI-compatible server (`xer.OpenAI`)

`xer.OpenAI.Server` wraps `Content::OpenAI::OpenAIServer` — the local
OpenAI-compat endpoint (`/v1/chat/completions`, `/v1/images/generations`,
`/v1/audio/*`, `/v1/models`) backed by `content/network`'s HTTP stack.
JS handlers register on the same router and are marshalled onto the
Node main thread automatically:

```js
const srv = new xer.OpenAI.Server({ host: '127.0.0.1', port: 8080 });
srv.on('/hello', (req, res) => res.json({ ok: true }));
srv.on('POST', '/echo', (req, res) => res.json({ body: req.body }));
srv.onHandleCompletion((req, res) => res.json({ choices: [] }));
srv.onHandlerKey((req, verify) => verify(true));   // optional auth gate
srv.start();
```

Request objects expose `method/path/query/headers/body/json/rawBody`;
responses expose `status()/setHeader()/json()/send()/end()`.

---

## YA.*.dll modular packaging

Static project libraries are packaged into shared `YA.*.dll` modules
(`cmake/YaDll.cmake` + generated `.def` exports, `cmake/GenYaDef.cmake`).
Executables link the YA DLLs implicitly; third-party dependencies are
embedded inside the YA modules as static archives, so binaries run
without any MinGW/MSYS2 runtime on `PATH`. Examples: `YA.HelperBase`,
`YA.DesginCSS`, `YA.MediaPipeline`, `YA.MediaAudio`,
`YA.MediaDecoderUniversal` (whole FFmpeg static closure),
`YA.LibXml2`, `YA.AudioCore`, `YA.VideoCore`.

## Status and known issues

- **`XER::` namespace migration** — complete across the codebase (~230 files),
  full build verified.
- **GPU**: D3D12+Ganesh OK; device-removed → raster fallback;
  `gpu_trace.log` in `%TEMP%/XER/`.
- **Crashes**: stackscan in crash_handler for rip=0/AV diagnostics.
- **Tests**: `XER_UI_Tests.exe` (UI + FFmpeg), `XER_Media_Smoke.exe` —
  21 checks: demux/decode/render pipeline, CSS matching/cascade,
  style invalidation, stylesheet parser, `BufferedDataSource`.
- **Delay-loaded DLLs**: ggml/whisper/shaderc/vulkan are delay-loaded by
  `YA.MediaDecoderUniversal.dll`; the binary starts without them and
  loads them on first use.
