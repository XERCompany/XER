# XER Editor

Нативный Windows-аудио/видеоредактор с визуальным редактором узловых графов
(NodeGraph), Lua-скриптингом UI (LSUI), модульной DSP/A-I обработкой аудио,
сетевым стеком и FFmpeg-интеграцией.

- **Язык:** C++23, Win32 API, Direct2D/Direct3D12, Skia (Ganesh), Lua 5.x
- **Сборка:** CMake 3.21+ + vcpkg (`x64-windows-release`)
- **Выходные бинарники:** `build2/bin/Editor.exe`, `XER_UI_Tests.exe`,
  `lsui.exe`, `EngineScanVST3.exe`

---

## Содержание

- [Сборка и запуск](#сборка-и-запуск)
- [Архитектура](#архитектура)
- [Карта модулей `src/`](#карта-модулей-src)
- [Узел графа: модель, executor, история](#node_graph)
- [UI: канвас, виджеты, NodeGraph-редактор](#ui)
- [Flux: core, terminal, renderer](#flux)
- [LSUI — Lua UI](#lsui)
- [Аудио: DSP, синтез, плагины, кодеки](#audio)
- [Сеть: sockets, HTTP, QUIC, WebSocket, S3](#network)
- [Видео: FFmpeg decoder, сессии, аудио-плеер](#video)
- [SQL: ORM, query builder, SQLite](#sql)
- [AsyncIO: потоки, память, hardware abstraction](#async_io)
- [Cache, Helper, Wrappers](#misc)
- [Примеры кода](#примеры-кода)

---

## Сборка и запуск

```bat
:: Требуется Visual Studio 2022 + vcpkg (C:\vcpkg)
cmake -S . -B build2 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build2 --config Release
```

| Цель | Бинарник | Назначение |
|---|---|---|
| `Editor` | `build2/bin/Editor.exe` | Главное приложение |
| `XER_UI_Tests` | `build2/bin/XER_UI_Tests.exe` | UI/FFmpeg тесты |
| `lsui` | `build2/bin/lsui.exe` | Lua UI рантайм |
| `EngineScanVST3` | `build2/bin/EngineScanVST3.exe` | Сканер VST3-плагинов |

```bat
build2\bin\Editor.exe            :: редактор (Projects → NodeGraph)
build2\bin\lsui.exe script.lua   :: Lua-скрипт с UI-окнами
build2\bin\XER_UI_Tests.exe      :: тесты
```

Кэш/логи/crash-дампы: `%TEMP%\XER\cache\` (crash-репорты — `cache\crashes\`).

---

## Архитектура

```
main.cc
 └─ src/app            Application shell: AppUI, окна, crash handler,
                       NodeGraph-ноды приложения (VST3/Lua/FX/S3/Network)
 └─ src/ui             UI-фреймворк: Window, Canvas (HWND+Skia/D3D12),
                       тема, layout, 80+ виджетов, NodeGraphEditorWidget
 └─ src/node_graph     Модель графа: Graph/Node/Link/Group, History,
                       Executor (параллельный, incremental, step-debug),
                       сериализация JSON, реестр типов, ProjectStore (SQLite)
 └─ src/flux           Мини-фреймворк: core, terminal emulator,
                       renderer, input, history — используется LSUI
 └─ src/lsui           lsui.exe — Lua-скрипты с UI-окнами,
                       watch(), Node.js-style require
 └─ src/audio          DSP-движок: biquad/IIR/FIR, FFT (MKL), синтез,
                       VST3/CLAP/AU хосты, эффекты, MIDI, кодеки
 └─ src/content        Контентные модули: network/* (полный сетевой стек),
                       video (FFmpeg), s3, hex, clipboard, audio player
 └─ src/wrapper        Мосты к внешним библиотекам: Skia, FFmpeg, CUDA,
                       cuDNN, ANGLE, OpenCV
 └─ src/sql            SQL-движок: SQLite storage, ORM, query builder
 └─ src/async_io       Async I/O: thread pool, буферы, prefetch,
                       hardware abstraction (CPU/GPU/thermals/NVML)
 └─ src/cache          Кэш-хранилище: persistent + temporary, scan/lock
 └─ src/helper         Утилиты: string_utils, buffer, dict, compression, diff
 └─ src/lunca          Мини-протокол packet/udp_socket (Lua network)
```

Все пространства имён унифицированы под корень `XER::`
(`XER::Engine::*`, `XER::Content::*`, `XER::UI::*`, `XER::NodeGraph`, …).

---

## Карта модулей `src/`

### `src/app/` — точка входа приложения

| Файл | Описание |
|---|---|
| `app_main.cc/.h` | `App::Main` — инициализация Flux/UI, запуск event loop |
| `app_ui.cc/.h` | `AppUI` — композиция окна: Projects browser ↔ NodeGraph editor ↔ Settings ↔ Video; `pending_action_` для отложенных переходов |
| `app_audio_controller.cc/.h` | `AudioController` — управление аудио-движком из UI |
| `app_audio_processor.cc/.h` | Обработка аудио-потока (effects chain) |
| `app_node_graph_manager.cc/.h` | Менеджер NodeGraph-проектов |
| `node_graph_app_nodes.cc/.h` | Регистрация прикладных NodeGraph-типов: `plugin.vst3`, `plugin.vst3_view`, `lua.*`, `fx.*`, `net.*`, `s3.*`, `io.*` — eval-функции, VST3/Lua state management |
| `crash_handler.cc/.h` | `App::Crash` — unhandled exception filter: minidump + crash.log + state.json + stackscan + авторестарт (max depth 2), `SetSessionProject`, `ConsumePendingReport` |

### `src/async_io/` — асинхронный I/O и hardware

| Файл | Описание |
|---|---|
| `io_thread_pool.*` | Общий пул рабочих потоков (`GetSharedInstance`) |
| `async_task_queue.*` | Очередь отложенных задач |
| `async_file_reader/writer.*` | Файловое чтение/запись через пул |
| `io_stream_reader/writer.*` | Потоковые обёртки над файлами |
| `io_buffer_pool.*`, `async_buffer_pool.*` | Пулы буферов (zero-alloc hot path) |
| `io_cache_manager.*` | Кэширование прочитанных данных |
| `io_file_map.*` | Memory-mapped файлы |
| `io_prefetch_manager.*` | Упреждающее чтение (readahead) |
| `io_priority_queue.*` | Приоритетная очередь задач |
| `dnn_backends/cudnn_*` | cuDNN-движки: bridge, convolution, pooling, activation |
| `hardware_abstraction/*` | CPU/GPU/Memory/Disk/Network/Battery info, `HwPowerMonitor` (RAPL), `HwTempSensor` + alert callback, `NvmlLoader` (NVIDIA NVML), `SystemInfoProvider` (async snapshot → JSON), `HwCapabilityScanner`, `HwTopologyMap`, `HwDirectIO` |
| `log_and_debug/*` | `IoPerfCounter`, `IoMemoryTracker`, `IoGpuUsageLogger`, `IoDumpHelper` |
| `sync_primitives/*` | `MutexWrapper`, `ReadWriteLock`, `Barrier`/`SyncBarrier`, `ConditionVariableWrapper`, `SyncEventListener`, `SyncSemaphoreCpu` |

### `src/audio/` — аудио-подсистема

#### `audio_core/` — движок реального времени

`AudioEngine`, `AudioEngineHost`, `AudioAsyncManager` (оффлоад на пул),
`AudioBufferManager`, `AudioFifoQueue`, `AudioClockSync`, `AudioLatencyMonitor`,
`AudioInterleaveProcessor`, `AudioBitDepthConverter`, `AudioSampleRateConverter`,
`AudioSourceLoader`, `AudioThreadPriority`, `AudioCacheManager`, `AudioLogger`.

#### `dsp_algorithms/` — DSP-примитивы

`DspBiquadProcessor` (biquad-фильтры), `DspIirFilterBank`, `DspFirFilterBank`,
`DspConvolutionEngine` (свёртка), `DspFftEngine` (**MKL DFTI**),
`DspStftProcessor`, `DspPhaseVocoder`, `DspPitchShifterGranular`,
`DspSpectralShaper`, `DspDelayLineCircular`, `DspWindowingFunctions`.

#### `effects_rack/` — 66 файлов: эффект-примитивы и цепочки

- **Динамика:** `fx_dynamics_compressor/expander/gate/limiter`
- **EQ:** `fx_eq_graphic`, `fx_eq_parametric`, `custom_effect_eq_engine`, `custom_effect_eq_lite`
- **Модуляция:** `fx_mod_chorus`, `fx_mod_flanger`, `fx_mod_phaser`, `fx_mod_extra`
- **Дисторшн:** `fx_distortion_tube`, `fx_distortion_bitcrush`
- **Реверб:** `fx_reverb_algorithmic`, `fx_reverb_convolution`
- **Питч:** `fx_pitch_auto_tune_correct`
- **Custom FX framework:** `custom_effect*` (core, flow, invoke, node,
  node_wrapper — CLAP-обёртка ноды, package, processor, algorithms,
  apply_multi, fxdata, struct, list, curve_line, accel, advanced_processing)

#### `plugin_wrappers/` — внешние плагины

`Vst3ExternalHost`/`Vst3HostInterface`/`Vst3PluginScanner` (сканирование
через `EngineScanVST3.exe`), `ClapExternalPluginHost`/`ClapHostInterface`/
`ClapMinimal`, `AuHostInterface` (macOS-заглушка), `PluginBuiltinHost`,
`PluginParameterBridge`.

#### `synth_engine/` — синтезатор

`SynthOscAnalog/Fm/Noise/Wavetable`, `SynthEnvAdsr`, `SynthEnvMultistage`,
`SynthLfoStandard`, `SynthFilterLadder`, `SynthModulationMatrix`,
`SynthPolyphonyController`, `SynthVoiceManager`.

#### `file_io_codecs/` — кодеки

`CodecWavPcm`, `CodecWavFloat`, `CodecFlacDecoder`, `CodecMp3Lame`,
`CodecOggVorbis`, `CodecAacAdts`, `CodecMetadataId3`,
`CodecFfmpegDecodeHelper`/`CodecFfmpegEncodeHelper`, `CodecFileStreamer`.

#### `analysis_ai/` — анализ и AI

`AudioFftAnalyzer`, `AudioPitchDetector`, `AnalPitchEstimator`,
`AnalSpectrogramGenerator`, `AnalBeatTracker`, `AnalOnsetDetection`,
`AnalLoudnessMeterLufs`, `AnalRmsPeakDetector`, `AnalHarmonicContent`,
`AiOpenvinoMusicRemark` (OpenVINO-инференс).

#### `midi_sequencing/`

`MidiParser`, `MidiEventDispatcher`, `MidiClockGenerator`,
`MidiPatternSequencer`, `MidiAutomationCurve`, `MidiControllerMapping`,
`MidiSysexHandler`.

#### `fx_customs/` — пресеты и готовые цепочки

`FxCustomsEffectBuilder`, `FxCustomsPresets`, `FxCustomsMasteringEffects`,
`FxCustomsSpatial(Effects)`, `FxCustomsVintageEffects`, `FxCustomsVocalEffects`,
`FxCustomsEffectUtils`.

#### `demo/` — CLI-демо и smoke-тесты

`AudioAnalysisCli`, `AudioDspDemo`, `AudioAnalysisSmoke`, `AudioModulesSmoke`,
`AudioFxCustom`.

### `src/node_graph/` — модель графа

| Файл | Описание |
|---|---|
| `node_graph.h/.cc` | `Graph`, `Node`, `PortRef`, `Link`, `Group`, `Value`, `NodeTypeDescriptor` — DAG-модель, валидация (циклы, типы портов, dangling links), dirty-tracking, topological order / execution levels |
| `node_graph_value.*` | `Value` — variant: none/number/bool/string/array |
| `node_graph_registry.*` | `NodeTypeRegistry` — регистрация типов нод без изменения ядра |
| `node_graph_executor.*` | `Executor` — параллельная оценка по уровням, incremental cache, external inputs/outputs, **step-debug**: `StepOnce(StepInfo)`/`ResetStep()`/`step_result()` |
| `node_graph_history.*` | `History` + `Command` (Apply/Revert/MergeWith), `MacroCommand`, `SnapshotCommand`, `GroupCommand` — undo/redo все правки включая группы |
| `node_graph_serializer.*` | JSON-сериализация графа, `kGraphFormatVersion`, валидация при загрузке |
| `node_graph_json.*` | JSON DOM (`Json::Value`-подобный) |
| `node_graph_builtins.*` | Встроенные типы: math, string, array (convolve через MKL FFT), io.*, graph.input/output/subgraph |
| `node_graph_ext.*` | Расширенные типы: MKL-ускоренные array.* (vdSin, cblas), string.* (UTF-8, regex, base64, crc32, compress) |
| `node_graph_project_store.*` | `ProjectStore` — SQLite-backed проекты: create/config/rename/delete, graphs, presets |

### `src/ui/` — UI-фреймворк

#### `core/`

`Window` (HWND + message loop, `frame_callback_` таймер),
`Application` (singleton, Run/Shutdown).

#### `canvas/`

`Canvas` — дочерний HWND поверх окна: растр через Skia surface **или**
GPU-путь D3D12+Ganesh (`TryInitGpu`, `GaneshD3DAllocator` committed
resources, flip-model swapchain, device-removed → raster fallback).
Input/render колбэки, clip stack, `DrawImageBgra` persistent texture.

#### `widgets/` — 80 виджетов

- **NodeGraph:** `node_graph_editor_widget.*` — главный редактор:
  pan/zoom-to-cursor, snap к сетке, drag нод/связей, box-select,
  copy/paste/duplicate, align/distribute, palette+поиск, контекстные меню
  (Add Node, Problems, Group/Ungroup, Collapse/Expand, Step Graph,
  Reset Debug), subgraphs, minimap, валидация-бейджи, autosave,
  inspector с port values после run.
  `node_param_host.*` — прозрачный HWND-overlay с нативными контролами
  параметров внутри нод (EDIT/COMBOBOX/CHECKBOX/file browse),
  click-through через `WS_EX_LAYERED`+colorkey.
- **Базовые:** `widget`, `widget_tree`, `button`, `label`, `checkbox`,
  `radio_group`, `select`, `switch`, `toggle`, `slider`, `text_input`,
  `textarea`, `badge`, `card`, `alert`, `dialog`, `context_menu`,
  `breadcrumb`, `pagination`, `progress`, `separator`, `skeleton`,
  `spinner`, `tabs`, `tooltip`, `scroll_area`, `accordion`, `avatar`
- **Специализированные:** `projects_widget` (Project browser:
  Select/Config/Duplicate/Delete + поиск), `settings_widget`,
  `fx_rack_widget` + `fx_rack_state` (цепочка эффектов),
  `eq_editor` (EQ-кривые), `waveform_widget`, `audio_player_widget`,
  `video_preview_widget`, `chain_map_widget`, `screen_loading_widget`
  (splash: проверка БД/проектов/кэша/драйверов)

#### `theme/`, `layout/`, `motion/`, `colorspace/`, `rendering/`, `platform/`, `backends/`

- `theme/` — shadcn-подобные токены светлой/тёмной темы (`Theme::Tokens`, `Theme::Color`)
- `layout/geometry.h` — Rect/Point/Size
- `motion/` — `Motion` (easing-анимации), `accel` (**MKL VML** для float- ops)
- `colorspace/` — `ColorSpace`, `ColorManager`, `HdrManager`
- `rendering/renderer.h` — интерфейс рендерера
- `rendering/windows/direct2d_renderer.*` — D2D-бэкенд
- `platform/windows/input_translator.*` — Win32 ввод → события
- `backends/flux/` — Flux-адаптеры: `FluxRenderer`, `FluxInputAdapter`
- `demo/audio_player_demo.cc` — демо

### `src/flux/` — минимальный фреймворк (ядро LSUI)

| Подмодуль | Файлы | Назначение |
|---|---|---|
| `core/` | `flux_core`, `flux_config`, `flux_context`, `logger`, `module_manager` | Инициализация, конфиг, логирование, модули |
| `terminal/` | `terminal_emulator`, `terminal_buffer`, `terminal_ansi_parser`, `terminal_colors`, `terminal_styles`, `terminal_extended`, `terminal_input_handler`, `terminal_interface`, `terminal_manager`, `terminal_output_renderer`, `terminal_size_detector`, `terminal_window` | Полный ANSI-терминал: буфер, parser, стили, fit/center/rule, snapshot |
| `rendering/` | `renderer`, `render_context`, `render_pipeline`, `frame_buffer` | Рендер-конвейер |
| `input/` | `input_manager`, `key_binding_manager`, `command_parser` | Ввод и биндинги |
| `history/` | `action_history`, `command_history` | История действий/команд |

### `src/lsui/` — lsui.exe: Lua UI

| Файл | Описание |
|---|---|
| `lsui.cc/.h` | `UiWindow` — окно на Canvas, Lua API: `ui.window{}`, `on_tick/on_mouse/on_key`, immediate-mode draw (`rect/text/button`...), `watch()` на чистом Lua, crash-filter в main |
| `lsui_term.cc` | `term.*` модуль поверх `flux/terminal`: print/println/write/write_err, clear/snapshot/text, size, fit/center/rule, fg/bg/style/reset |
| `main.cc` | Точка входа: Lua state, `kLuaHelpers` — **Node.js-style require**: `./rel`, `../rel`, `node_modules` walk-up, `LSUI_PATH`, `mod.lua`/`mod/init.lua`, `module.exports`/`exports`, circular-require partial exports, `require.resolve`, `__dirname`/`__filename`/`module`/`exports` |

### `src/wrapper/` — мосты к внешним библиотекам (`engine::bridge::*`)

| Модуль | Файлы | Назначение |
|---|---|---|
| `skia/` | `skia_engine_bridge` + `_codec_extra`, `_fx`, `_image_filter`, `_lottie`, `_m44`, `_pdf`, `_shader`, `_svg` | Skia API: image codecs, color filters, shaders, SVG/Lottie/PDF, matrices |
| `ffmpeg/` | `ffmpeg_engine_bridge`, `ffmpeg_video_decoder` | FFmpeg: decoder с audio-tap (`EnableAudioTap`, `ReadAudioFrames` на отдельном потоке, packet queue под mutex) |
| `cuda/` | `cuda_engine_bridge` | CUDA runtime bridge |
| `cudnn/` | `cudnn_engine_bridge` | cuDNN bridge |
| `angle/` | `angle_engine_bridge` | ANGLE (OpenGL ES → D3D) |
| `opencv/` | `opencv_engine_bridge` | OpenCV bridge |

### `src/content/` — контентные модули (`XER::Content::*`)

#### `network/` — сетевой стек (~100 файлов, namespace `XER::network::*`)

| Подмодуль | Файлы | Назначение |
|---|---|---|
| `socket/` | `tcp_client_socket`, `udp_client_socket`, `ssl_client_socket`, `socket_client`, `socket_server`, `socket_stream`, `socket_proxy`, `network_selector`, `net_log` + `platform/{windows,posix}/socket_platform_*` | Сокеты: TCP/UDP/SSL, selector, proxy, платформенные абстракции |
| `ssl/` | `ssl_context`, `ssl_session_cache` | SSL-окружение и кэш сессий |
| `cert/` | `cert_database`, `cert_storage`, `cert_transparency`, `cert_verifier`, `cert_verify_result`, `nss_certificate`, `scoped_nss_types` | X.509/NSS-сертификаты, CT-логи, верификация |
| `dns/` | `dns_client`, `dns_resolver`, `dns_config_service`, `dns_record_type`, `host_resolver`, `host_resolver_impl` | DNS-резолвер |
| `http/` | `http_auth`, `http_cache`, `http_network_delegate`, `http_network_session`, `http_request/response_headers`, `http_server_properties`, `http_stream`, `http_stream_factory`, `http_sync_request`, `http_transaction`, `http_util` | HTTP/1.1 клиент: сессии, транзакции, кэш, auth |
| `quic/` | `quic_connection`, `quic_crypto_stream`, `quic_network_delegate/session`, `quic_packet`, `quic_protocol`, `quic_server_id`, `quic_stream`, `quic_version_manager` | QUIC-протокол |
| `spdy/` | `spdy_framer`, `spdy_network_delegate`, `spdy_protocol`, `spdy_session`, `spdy_stream` | SPDY-протокол |
| `websocket/` | `websocket_frame`, `websocket_handshake_stream`, `websocket_network_delegate`, `websocket_stream`, `websocket_event_interface` | WebSocket-клиент (RFC6455) |
| `ftp/` | `ftp_ftp_request`, `ftp_ftp_stream`, `ftp_ftp_network_delegate` | FTP-клиент |
| `url/` | `url_request`, `url_request_context(+getter)`, `url_request_intercepting_job`, `url_request_job`, `url_request_util`, `url_util` | URL-запросы и парсинг |
| `proxy/` | `proxy_config`, `proxy_info`, `proxy_resolver(+factory)`, `proxy_service`, `socks_client_socket` | HTTP/SOCKS-прокси |
| `transport/` | `transport_client_socket`, `transport_server_socket`, `transport_io_buffer`, `transport_network_delegate/session` | Транспортный слой |
| `core/` | `network_context`, `network_service`, `network_service_delegate`, `network_service_util`, `network_task_runner` | Сетевой сервис и контекст |
| `async/` | `async_operation_manager` + `iocp/iocp_completion_port`, `posix/epoll_async_socket` | Async-операции: IOCP (Windows) / epoll (POSIX) |
| `cache/` | `network_cache_manager` | Кэш HTTP-ответов |
| `logging/` | `network_logger` | Сетевое логирование |
| `network_util/` | `network_util` | Утилиты |

#### Прочие content-модули

| Модуль | Файлы | Назначение |
|---|---|---|
| `video/` | `video_session` (фоновый decode-worker), `video_decoder`, `video_engine`, `video_audio_player` (WASAPI streaming из shared decoder), `video_frame(+cache)`, `video_info`, `video_command_buffer` | Видео: FFmpeg-декодер, play/pause/seek, аудиодорожка через тот же контейнер |
| `s3/` | `s3_client`, `s3_signer` (AWS Signature V4), `s3_types` | S3-совместимое хранилище |
| `hex/` | `hex.h` (+`detail`) | HEX-кодирование/декодирование байт |
| `clipboard/` | `clipboard` | Буфер обмена |
| `audio/` | `audio_player` | Аудио-плеер контента |

### `src/sql/` — SQL-движок (`XER::Engine::SQL::*`)

| Подмодуль | Файлы | Назначение |
|---|---|---|
| `storage/` | `sqlite_database` | SQLite-обёртка: open/exec/prepare/transaction |
| `modern/` | `query_builder`, `query` | Fluent query builder (`SELECT/INSERT/UPDATE/DELETE`) |
| `orm/` | `table_schema` | Схемы таблиц для ORM |
| `content/` | `value`, `row` | `Value` (SQL-типы), `Row` |
| `plus/` | `transaction` | Транзакции |

### `src/cache/` — кэш-хранилище (`XER::Engine::Cache`)

`CacheManager` (singleton, `Persistent()`/`Temporary()` roots),
`PersistentStorage`, `TemporaryStorage`, `CacheStorage`, `CacheEntry`,
`CacheConfiguration`, `CacheConstants`, `CacheEncryptionHandler`,
`CacheScanAndLock`, `CacheUtils`.

### `src/helper/` — утилиты (`XER::Engine::Helper`)

`string_utils` + `helper/string` (String helpers, UTF-8),
`buffer` (Buffer), `dict` (Dict), `file_diff` (diff),
`compression_codec` (zlib), `jsoncpp_helper` (Json::Value мост),
`tool_to` (Tool::ToString и пр.).

### `src/lunca/` — Lua-сеть (`common/`)

`packet` (упаковка пакетов), `udp_socket` — минимальный UDP для Lua.

### Корневые файлы

| Файл | Описание |
|---|---|
| `main.cc` | Точка входа: crash-handler install → App::Main |
| `app_command.h/.cc` | `XER::AppCommand` — CLI-парсер команд (в `namespace XER`) |
| `engine_params.h/.cc` | `XER::EngineParams` — runtime-конфиг из CLI/env |
| `audio_config_helper.h/.cc` | Применение audio-JSON конфига к командам |
| `json_config_helper.h/.cc` | JSON-конфиг хелперы |
| `tools/EngineScanVST3/main.cc` | Утилита сканирования VST3-плагинов (путь, имя, параметры) |

---

## `<a id="node_graph"></a>`NodeGraph: модель, executor, история

### Модель (`Graph`)

```cpp
#include "node_graph/node_graph.h"

XER::NodeGraph::Graph graph(registry);

auto& math = graph.AddNode("math.add");       // NodeId
graph.MoveNode(math.id, 100.0, 50.0);

// Порты и связи
graph.Connect({src_node, "out"}, {dst_node, "in"});

// Группы (визуальные рамки)
XER::NodeGraph::Group grp;
grp.title = "DSP chain"; grp.x = 0; grp.y = 0; grp.w = 400; grp.h = 300;
grp.color = "#6366f1";
graph.AddGroup(grp);

// Валидация
for (const auto& issue : graph.Validate()) {
    // issue.severity / issue.message / issue.node
}
```

### Executor — запуск, incremental, step-debug

```cpp
XER::NodeGraph::Executor ex(graph);

// Полный запуск
XER::NodeGraph::ExecutionResult res = ex.Run();
// res.ok / res.evaluated / res.cached / res.node_errors / res.logs

// Пошаговое выполнение (по одному execution-уровню за вызов)
XER::NodeGraph::Executor::StepInfo step;
while (ex.StepOnce(&step)) {
    // step.level, step.total_levels, step.nodes — ноды этого шага
    // port values уже в кэше:
    auto v = ex.GetOutput(step.nodes.front(), "out");
}
ex.ResetStep();          // сброс сессии
ex.InvalidateCache();    // полная инвалидация
```

### History — undo/redo

```cpp
XER::NodeGraph::History hist(graph);
hist.BeginMacro("Move nodes");
hist.MoveNode(id, x, y);
hist.AddGroup(std::move(grp));
hist.EndMacro();
hist.Undo();   // откат всего macro
hist.Redo();
```

### Сериализация и проекты

```cpp
#include "node_graph/node_graph_serializer.h"
#include "node_graph/node_graph_project_store.h"

std::string json = XER::NodeGraph::SerializeGraph(graph);
XER::NodeGraph::Graph loaded(registry);
XER::NodeGraph::DeserializeGraph(json, &loaded);   // проверка версии

// SQLite-backed проекты
XER::NodeGraph::ProjectStore store(db_path);
store.CreateProject("My Project", &err);
store.SaveGraph(project_id, "main", graph);
```

---

## `<a id="ui"></a>`UI: канвас, виджеты, NodeGraph-редактор

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

GPU-путь: D3D12 + Ganesh, собственный `GaneshD3DAllocator`
(committed resources), flip-model swapchain, fallback на raster
при device-removed.

### NodeGraphEditorWidget

```cpp
auto editor = std::make_shared<XER::UI::Widgets::NodeGraphEditorWidget>(
    hwnd, theme_hook, project_id);
editor->SetHostVisible(true);          // canvas + param overlay
editor->SetPluginEditorHandler(...);   // VST3 editor HWND
editor->SetBrowserBackHook(...);       // кнопка "назад"
window->SetContent(editor);
```

Возможности: pan/zoom-to-cursor, grid snap, drag нод/связей,
box-select, multi-select, copy/paste/duplicate, align/distribute,
поиск нод (палитра), контекстные меню, subgraphs, minimap,
группы (Group/Ungroup Selection), collapse нод (header+порты),
валидация-бейджи + Problems-меню, step-debug (Step Graph/Reset Debug),
автосохранение, inspector с port values.

### Виджеты

Все виджеты — immediate-mode поверх `Renderer` + `WidgetTree`:
`Button`, `Label`, `Checkbox`, `Select`, `Slider`, `TextInput`,
`Dialog`, `ContextMenu`, `Tabs`, `ScrollArea`, `ProjectsWidget`,
`SettingsWidget`, `FxRackWidget`, `EqEditor`, `WaveformWidget`,
`AudioPlayerWidget`, `VideoPreviewWidget`, `NodeParamHost` и др.

Тема — shadcn-подобная: `Theme::Tokens` (light/dark),
`Theme::Color{r,g,b,a}`, `Mix`, `AccentC`, `DangerC`, `WarnC`.

---

## `<a id="flux"></a>`Flux: core, terminal, renderer

Flux — минимальный фреймворк, на котором построен LSUI (и частично UI).

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
- `flux/terminal/` — `TerminalEmulator` + `TerminalBuffer` + `TerminalAnsiParser` + стили/цвета: `print/println/write/write_err`, `clear/snapshot/text`, `size()`, `fit/center/rule`, `fg/bg/style/reset`
- `flux/rendering/` — `Renderer`, `RenderContext`, `RenderPipeline`, `FrameBuffer`
- `flux/input/` — `InputManager`, `KeyBindingManager`, `CommandParser`
- `flux/history/` — `ActionHistory`, `CommandHistory`

---

## `<a id="lsui"></a>`LSUI — Lua UI

`lsui.exe script.lua` — запускает Lua-скрипт с UI-окнами.

```lua
-- demo.lua
local ui = require("lsui")

local win = ui.window({
    title = "My App",
    width = 400, height = 300,
    on_tick = function(w, dt) end,
})

win:button({ x = 10, y = 10, w = 100, h = 30, label = "Click" })

ui.watch(1.0, function()          -- каждые 1 сек
    print("tick")
end)

-- require как в Node.js:
local util = require("./lib/util")        -- относительно файла
local pkg  = require("mylib")             -- node_modules walk-up / LSUI_PATH
```

Терминальный модуль:

```lua
local term = require("term")
term.println("Hello", { fg = "bright_blue", bold = true })
term.rule("=")
local w, h = term.size()
local snap = term.snapshot()
```

---

## `<a id="audio"></a>`Аудио: DSP, синтез, плагины, кодеки

### DSP-примитивы (`audio/dsp_algorithms`)

```cpp
#include "audio/dsp_algorithms/dsp_biquad_processor.h"
#include "audio/dsp_algorithms/dsp_fft_engine.h"

XER::Engine::Audio::DSP::BiquadProcessor biquad;
biquad.SetLowpass(44100.0, 1000.0, 0.707);
biquad.Process(samples.data(), samples.size());

XER::Engine::Audio::DSP::FFTEngine fft(1024);   // MKL DFTI
fft.Forward(real_in, complex_out);
```

### VST3/CLAP хостинг

```cpp
#include "audio/plugin_wrappers/vst3_external_host.h"
#include "audio/plugin_wrappers/clap_external_plugin_host.h"

XER::Engine::Audio::Plugin::Vst3ExternalHost host;
host.Load("C:/Plugins/MySynth.vst3", 44100.0, 512, &err);
host.SetParameter(0, 0.75f);
host.Process(in_block, out_block, 512);
```

### NodeGraph audio-ноды (`app/node_graph_app_nodes`)

```cpp
// Регистрация типов: plugin.vst3, plugin.vst3_view, lua.buffer,
// fx.<algo>, net.websocket_*, s3.*, io.file_*, audio.*
// VST3/Lua states живут в g_vst3_hosts / g_lua_states под mutex.
```

---

## `<a id="network"></a>`Сеть: sockets, HTTP, QUIC, WebSocket, S3

```cpp
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/websocket/websocket_stream.h"
#include "content/network/http/http_sync_request.h"

// TCP
XER::Content::Network::TcpClientSocket sock;
sock.Connect("example.com", 80, &err);

// WebSocket (NodeGraph нода net.websocket_send)
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

## `<a id="video"></a>`Видео: FFmpeg decoder, сессии, аудио-плеер

```cpp
#include "content/video/video_session.h"
#include "content/video/video_audio_player.h"

XER::Content::Video::VideoSession session;
session.Open("video.mp4", &err);     // фоновый decode-worker
session.Play(); session.Pause(); session.Seek(10.0);

XER::Content::Video::VideoAudioPlayer player;
player.Attach(session.GetAvDecoder());         // тот же контейнер!
player.Play();
```

`VideoDecoder` — FFmpeg wrapper: video-кадры на одном потоке,
`ReadAudioFrames` на втором (audio packets через mutex queue),
`EnableAudioTap(true)` — аудио из того же открытого контейнера.

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

## `<a id="async_io"></a>`AsyncIO: потоки, память, hardware

```cpp
#include "async_io/io_thread_pool.h"
#include "async_io/hardware_abstraction/system_info.h"

auto& pool = XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();
pool.Enqueue([] { /* worker task */ });
pool.WaitAll();                      // дождаться очереди

// Системная информация (async snapshot → JSON)
auto fut = XER::AsyncIO::HardwareAbstraction::SystemInfoProvider::Instance()
    .GetSnapshotAsync();
```

`HwTempSensor` — мониторинг температур с alert-callback;
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

Wrappers (`engine::bridge::*` → теперь `XER::engine::bridge::*`):
Skia (полный графический API + кодеки + SVG/Lottie/PDF), FFmpeg,
CUDA, cuDNN, ANGLE, OpenCV — тонкие C++ обёртки над C API.

---

## `<a id="примеры-кода"></a>`Примеры кода

### 1. NodeGraph: создать граф, добавить ноды, соединить, сохранить

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
RegisterExtendedNodes(registry);      // MKL-ускоренные расширения

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

// Сохранить
std::string json = SerializeGraph(graph);
```

### 2. NodeGraph: группы + collapse + валидация

```cpp
// Группа через History (undoable)
Group grp{.title = "Chain", .x = -20, .y = -30, .w = 500, .h = 200,
          .color = "#6366f1"};
hist.AddGroup(grp);

// Валидация
for (const auto& issue : graph.Validate()) {
    if (issue.severity == ValidationIssue::Severity::kError) {
        printf("ERROR node %llu: %s\n",
               (unsigned long long)issue.node, issue.message.c_str());
    }
}

// Editor-side collapse: Level::collapsed_nodes — set<NodeId>
// Свёрнутая нода = header + порты (без param-рядов), связи работают.
```

### 3. LSUI: окно + require + watch

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

### 4. VST3 plugin в NodeGraph

```lua
-- В NodeGraph добавьте ноду plugin.vst3, params:
--   path = "C:/Plugins/Serum.vst3"
--   sampleRate = 44100
--   p0..p7 = normalized params (0..1)
-- После Run графа editor автоматически откроет plugin GUI.
```

### 5. FFmpeg video + audio

```cpp
XER::Content::Video::VideoSession vs;
vs.Open("movie.mkv");

// Аудио из того же декодера:
XER::Content::Video::VideoAudioPlayer ap;
ap.AttachDecoder(vs.SharedDecoder());
ap.Play();
vs.Play();
// SyncTo() держит playhead'ы синхронными (drift > 300ms → seek).
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

### 7. Crash handler (уже встроен в main.cc)

```cpp
// main.cc:
XER::App::Crash::Install();          // до всего остального
// При краше: minidump + crash.log + state.json →
// %TEMP%/XER/cache/crashes/<timestamp>/, авторестарт (--crash-depth<=2)
// stackscan в crash.log показывает return-адреса без frame pointers.
```

---

## Статус и известные проблемы

- **Namespace-миграция `XER::`** — завершена по коду (~230 файлов),
  полная сборка верифицируется.
- **GPU**: D3D12+Ganesh OK; device-removed → raster fallback;
  `gpu_trace.log` в `%TEMP%/XER/`.
- **Краши**: stackscan в crash_handler для диагностики rip=0/AV.
- **Тесты**: `XER_UI_Tests.exe` — 39 тестов (UI + FFmpeg).
