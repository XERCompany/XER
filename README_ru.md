# XER Editor

[English](README_en.md) | [Русский](README_ru.md)

Нативный Windows аудио/видео редактор с визуальным редактором нод-графов
(NodeGraph), UI на Lua-скриптах (LSUI), модульной DSP/AI обработкой аудио,
полным медиа-конвейером (`src/media`), демо с Chromium-браузером и VST3-хостом,
полным сетевым стеком и интеграцией FFmpeg.

- **Язык:** C++23, Win32 API, Direct2D/Direct3D12, Skia (Ganesh), Lua 5.x
- **Сборка:** CMake 3.21+ + Ninja; основной Windows-пресет
  `windows-clang-msys2` (тулчейн MSYS2 clang64), альтернатива — vcpkg + MSVC
- **Выходные бинарники:** `out/build/windows-clang-msys2/bin/Editor.exe`,
  `XER_UI_Tests.exe`, `XER_Media_Smoke.exe`, `lsui.exe`,
  `EngineScanVST3.exe`, `XER_VST3_CEF.exe`
- **Структура рантайма:** исполняемые файлы + `YA.*.dll` — все зависимости
  MSYS2 линкуются статически, MinGW-DLL поставлять не нужно
  (ggml/whisper/shaderc/vulkan грузятся через delay-load)

---

## Содержание

- [Сборка и запуск](#build-and-run)
- [Архитектура](#architecture)
- [Карта модулей `src/`](#module-map-of-src)
- [NodeGraph: модель, исполнитель, история](#node_graph)
- [UI: canvas, виджеты, редактор NodeGraph](#ui)
- [Flux: ядро, терминал, рендерер](#flux)
- [LSUI — UI на Lua](#lsui)
- [Аудио: DSP, синтезатор, плагины, кодеки](#audio)
- [Сеть: сокеты, HTTP, QUIC, WebSocket, S3](#network)
- [Видео: декодер FFmpeg, сессии, аудиоплеер](#video)
- [SQL: ORM, построитель запросов, SQLite](#sql)
- [AsyncIO: потоки, память, аппаратная абстракция](#async_io)
- [Кэш, Helper, обёртки](#misc)
- [Примеры кода](#code-examples)
- [Аддон для Node.js](#node-addon)

---

## <a id="build-and-run"></a>Сборка и запуск

### Windows (MSYS2 clang — основной пресет)

```bat
:: Требуется тулчейн MSYS2 clang64 в C:\tools\msys64\clang64
cmake --preset windows-clang-msys2
cmake --build --preset windows-clang-msys2
```

Результат в `out/build/windows-clang-msys2/bin/` — исполняемые файлы
импортируют только `YA.*.dll` (модули проекта) и системные DLL Windows.
Все библиотеки MSYS2 (ICU, OpenSSL, curl, FFmpeg, sqlite, zstd, brotli, …)
встроены статически через `xer_retarget_imported_to_static` в корневом
`CMakeLists.txt`; четыре зависимости, существующие только как DLL
(ggml, whisper, shaderc, vulkan), delay-load'ятся из
`YA.MediaDecoderUniversal.dll`.

### Windows (MSVC + vcpkg)

```bat
:: Требуется Visual Studio 2022 + vcpkg (C:\vcpkg)
cmake -S . -B build2 -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build2 --config Release
```

### Linux / macOS

Зависимости гибридные: системные пакеты (apt / Homebrew) для всего, что есть
в репозиториях ОС, и частичный vcpkg только для портов без системного аналога
(skia, ggml, folly, onnxruntime, …). Маппинг имён — в `cmake/deps/`,
тулчейн-файлы — в `cmake/platforms/`.

```bash
# Linux (Ubuntu 24.04): список пакетов — шаг "Install build tools +
# system dependencies (apt)" в .github/workflows/build.yml, затем:
cmake --preset linux-release
cmake --build --preset linux-release

# macOS:
cmake --preset macos-arm64   # Apple Silicon
cmake --preset macos-x64     # Intel
cmake --build --preset macos-arm64
```

Для частичного vcpkg: `-DXER_VCPKG_ROOT=<путь-к-vcpkg> -DXER_VCPKG_TRIPLET=x64-linux`
(или `arm64-osx`/`x64-osx`). cuDNN на Linux требует CUDA toolkit
(`cuda-toolkit-12-6` из apt-репозитория NVIDIA) — GPU не нужна, только nvcc.

| Цель | Бинарник | Назначение |
|---|---|---|
| `Editor` | `bin/Editor.exe` | Основное приложение |
| `XER_UI_Tests` | `bin/XER_UI_Tests.exe` | Тесты UI/FFmpeg |
| `XER_Media_Smoke` | `bin/XER_Media_Smoke.exe` | Смоук-тесты медиа-конвейера и CSS-движка |
| `lsui` | `bin/lsui.exe` | Рантайм Lua UI |
| `EngineScanVST3` | `bin/EngineScanVST3.exe` | Сканер/пробник VST3-плагинов (`--probe`, `--jobs N`) |
| `XER_VST3_CEF` | `bin/XER_VST3_CEF.exe` | Демо: редактор VST3 + браузер CEF + аудио-цепочка |

```bat
bin\Editor.exe                   :: редактор (Projects → NodeGraph)
bin\lsui.exe script.lua          :: Lua-скрипт с окнами UI
bin\XER_UI_Tests.exe             :: тесты UI
bin\XER_Media_Smoke.exe          :: смоук-тесты media + CSS (21 проверка)
bin\EngineScanVST3.exe           :: скан VST3 (async, --jobs = число ядер)
bin\XER_VST3_CEF.exe             :: демо VST3 + CEF
```

Кэш/логи/краш-дампы: `%TEMP%\XER\cache\` (краш-репорты — `cache\crashes\`).

---

## <a id="architecture"></a>Архитектура

```
main.cc
 └─ src/app            Оболочка приложения: AppUI, окна, crash handler,
                       ноды NodeGraph приложения (VST3/Lua/FX/S3/Network)
 └─ src/ui             UI-фреймворк: Window, Canvas (HWND+Skia/D3D12),
                       тема, layout, 80+ виджетов, NodeGraphEditorWidget,
                       css/ — движок стилей (селекторы, каскад, инвалидация)
 └─ src/node_graph     Модель графа: Graph/Node/Link/Group, History,
                       Executor (параллельный, инкрементальный, step-debug),
                       JSON-сериализация, реестр типов, ProjectStore (SQLite)
 └─ src/flux           Мини-фреймворк: ядро, эмулятор терминала,
                       рендерер, ввод, история — основа LSUI
 └─ src/lsui           lsui.exe — Lua-скрипты с окнами UI,
                       watch(), require в стиле Node.js
 └─ src/audio          DSP-движок: biquad/IIR/FIR, FFT (MKL), синтезатор,
                       хосты VST3/CLAP/AU, эффекты, MIDI, кодеки,
                       virtual audio cable (межпроцессные PCM-каналы)
 └─ src/content        Контент-модули: network/* (полный сетевой стек),
                       video (FFmpeg), s3, hex, clipboard, аудиоплеер
 └─ src/wrapper        Мосты к внешним библиотекам: Skia, FFmpeg, CUDA,
                       cuDNN, ANGLE, OpenCV
 └─ src/sql            SQL-движок: хранилище SQLite, ORM, построитель запросов
 └─ src/async_io       Асинхронный I/O: пул потоков, буферы, prefetch,
                       аппаратная абстракция (CPU/GPU/температуры/NVML)
 └─ src/cache          Кэш-хранилище: persistent + temporary, scan/lock
 └─ src/helper         Утилиты: string_utils, buffer, dict, compression,
                       diff, SpanReader/SpanWriter, raw_ptr
 └─ src/lunca          Минимальный протокол packet/udp_socket (Lua network)
 └─ src/media          Медиа-конвейер: демуксеры (file/URL/buffered/MSE),
                       декодеры, рендереры, приёмники, муксеры, плеер,
                       роутер, захват, сессия — упаковано в YA.*.dll
 └─ src/cef            Хост браузера CEF (встраиваемые Chromium-view)
 └─ demo/              Демо XER_VST3_CEF — встраивание редактора VST3 +
                       CEF-view + загрузка аудио/WASAPI/VST3-цепочка
 └─ tools/             Утилиты: EngineScanVST3, XERVirtualCable,
                       аудио/видео CLI
```

Все пространства имён объединены под корнем `XER::`
(`XER::Engine::*`, `XER::Content::*`, `XER::UI::*`, `XER::NodeGraph`,
`XER::Media::*`, …).

---

## <a id="module-map-of-src"></a>Карта модулей `src/`

### `src/app/` — точка входа приложения

| Файл | Описание |
|---|---|
| `app_main.cc/.h` | `App::Main` — инициализация Flux/UI, запуск цикла событий |
| `app_ui.cc/.h` | `AppUI` — композиция окон: браузер проектов ↔ редактор NodeGraph ↔ настройки ↔ видео; `pending_action_` для отложенных переходов |
| `app_audio_controller.cc/.h` | `AudioController` — управление аудио-движком из UI |
| `app_audio_processor.cc/.h` | Обработка аудиопотока (цепочка эффектов) |
| `app_node_graph_manager.cc/.h` | Менеджер проектов NodeGraph |
| `node_graph_app_nodes.cc/.h` | Регистрация типов нод приложения: `plugin.vst3`, `plugin.vst3_view`, `lua.*`, `fx.*`, `net.*`, `s3.*`, `io.*` — eval-функции, управление состоянием VST3/Lua |
| `crash_handler.cc/.h` | `App::Crash` — фильтр необработанных исключений: minidump + crash.log + state.json + stackscan + авто-перезапуск (глубина до 2), `SetSessionProject`, `ConsumePendingReport` |

### `src/async_io/` — асинхронный I/O и аппаратура

| Файл | Описание |
|---|---|
| `io_thread_pool.*` | Общий пул воркеров (`GetSharedInstance`) |
| `async_task_queue.*` | Очередь отложенных задач |
| `async_file_reader/writer.*` | Чтение/запись файлов через пул |
| `io_stream_reader/writer.*` | Потоковые обёртки над файлами |
| `io_buffer_pool.*`, `async_buffer_pool.*` | Пулы буферов (zero-alloc на горячем пути) |
| `io_cache_manager.*` | Кэширование прочитанных данных |
| `io_file_map.*` | Файлы, отображённые в память |
| `io_prefetch_manager.*` | Опережающее чтение (prefetch) |
| `io_priority_queue.*` | Приоритетная очередь задач |
| `dnn_backends/cudnn_*` | Движки cuDNN: bridge, convolution, pooling, activation |
| `hardware_abstraction/*` | Инфо о CPU/GPU/памяти/дисках/сети/батарее, `HwPowerMonitor` (RAPL), `HwTempSensor` + callback алертов, `NvmlLoader` (NVIDIA NVML), `SystemInfoProvider` (асинхронный снимок → JSON), `HwCapabilityScanner`, `HwTopologyMap`, `HwDirectIO` |
| `log_and_debug/*` | `IoPerfCounter`, `IoMemoryTracker`, `IoGpuUsageLogger`, `IoDumpHelper` |
| `sync_primitives/*` | `MutexWrapper`, `ReadWriteLock`, `Barrier`/`SyncBarrier`, `ConditionVariableWrapper`, `SyncEventListener`, `SyncSemaphoreCpu` |

### `src/audio/` — аудио-подсистема

#### `audio_core/` — движок реального времени

`AudioEngine`, `AudioEngineHost`, `AudioAsyncManager` (выгрузка в пул),
`AudioBufferManager`, `AudioFifoQueue`, `AudioClockSync`,
`AudioLatencyMonitor`, `AudioInterleaveProcessor`,
`AudioBitDepthConverter`, `AudioSampleRateConverter`,
`AudioSourceLoader`, `AudioThreadPriority`, `AudioCacheManager`,
`AudioLogger`.

#### `dsp_algorithms/` — DSP-примитивы

`DspBiquadProcessor` (biquad-фильтры), `DspIirFilterBank`,
`DspFirFilterBank`, `DspConvolutionEngine` (свёртка),
`DspFftEngine` (**MKL DFTI**), `DspStftProcessor`, `DspPhaseVocoder`,
`DspPitchShifterGranular`, `DspSpectralShaper`, `DspDelayLineCircular`,
`DspWindowingFunctions`.

#### `effects_rack/` — 66 файлов: примитивы и цепочки эффектов

- **Динамика:** `fx_dynamics_compressor/expander/gate/limiter`
- **EQ:** `fx_eq_graphic`, `fx_eq_parametric`, `custom_effect_eq_engine`, `custom_effect_eq_lite`
- **Модуляция:** `fx_mod_chorus`, `fx_mod_flanger`, `fx_mod_phaser`, `fx_mod_extra`
- **Дисторшн:** `fx_distortion_tube`, `fx_distortion_bitcrush`
- **Реверберация:** `fx_reverb_algorithmic`, `fx_reverb_convolution`
- **Питч:** `fx_pitch_auto_tune_correct`
- **Фреймворк кастомных FX:** `custom_effect*` (core, flow, invoke, node,
  node_wrapper — обёртка CLAP-нод, package, processor, algorithms,
  apply_multi, fxdata, struct, list, curve_line, accel, advanced_processing)

#### `plugin_wrappers/` — внешние плагины

`Vst3ExternalHost`/`Vst3HostInterface`/`Vst3PluginScanner` (сканирование
через `EngineScanVST3.exe` — по дочернему процессу `--probe` на модуль,
`std::async` с ограничением по числу ядер CPU),
`ClapExternalPluginHost`/`ClapHostInterface`/`ClapMinimal`,
`AuHostInterface` (заглушка macOS), `PluginBuiltinHost`,
`PluginParameterBridge`.

#### `audio_core/` — virtual audio cable

`VirtualAudioCable` — именованные межпроцессные PCM-каналы + мосты WASAPI:
`cable_fx_chain` (встроенные эффекты + VST3-стадии на канал),
`cable_wasapi` (мост к устройству), `live_audio_output`,
`process_audio_capture` (захват по процессам). CLI: `tools/XERVirtualCable`.

#### `synth_engine/` — синтезатор

`SynthOscAnalog/Fm/Noise/Wavetable`, `SynthEnvAdsr`,
`SynthEnvMultistage`, `SynthLfoStandard`, `SynthFilterLadder`,
`SynthModulationMatrix`, `SynthPolyphonyController`, `SynthVoiceManager`.

#### `file_io_codecs/` — кодеки

`CodecWavPcm`, `CodecWavFloat`, `CodecFlacDecoder`, `CodecMp3Lame`,
`CodecOggVorbis`, `CodecAacAdts`, `CodecMetadataId3`,
`CodecFfmpegDecodeHelper`/`CodecFfmpegEncodeHelper`, `CodecFileStreamer`.

#### `analysis_ai/` — анализ и AI

`AudioFftAnalyzer`, `AudioPitchDetector`, `AnalPitchEstimator`,
`AnalSpectrogramGenerator`, `AnalBeatTracker`, `AnalOnsetDetection`,
`AnalLoudnessMeterLufs`, `AnalRmsPeakDetector`, `AnalHarmonicContent`,
`AiOpenvinoMusicRemark` (инференс OpenVINO).

#### `midi_sequencing/`

`MidiParser`, `MidiEventDispatcher`, `MidiClockGenerator`,
`MidiPatternSequencer`, `MidiAutomationCurve`, `MidiControllerMapping`,
`MidiSysexHandler`.

#### `fx_customs/` — пресеты и готовые цепочки

`FxCustomsEffectBuilder`, `FxCustomsPresets`,
`FxCustomsMasteringEffects`, `FxCustomsSpatial(Effects)`,
`FxCustomsVintageEffects`, `FxCustomsVocalEffects`, `FxCustomsEffectUtils`.

#### `demo/` — CLI-демо и смоук-тесты

`AudioAnalysisCli`, `AudioDspDemo`, `AudioAnalysisSmoke`,
`AudioModulesSmoke`, `AudioFxCustom`.

### `src/node_graph/` — модель графа

| Файл | Описание |
|---|---|
| `node_graph.h/.cc` | `Graph`, `Node`, `PortRef`, `Link`, `Group`, `Value`, `NodeTypeDescriptor` — DAG-модель, валидация (циклы, типы портов, висячие связи), dirty-tracking, топологический порядок / уровни исполнения |
| `node_graph_value.*` | `Value` — variant: none/number/bool/string/array |
| `node_graph_registry.*` | `NodeTypeRegistry` — регистрация типов нод без изменения ядра |
| `node_graph_executor.*` | `Executor` — параллельное исполнение по уровням, инкрементальный кэш, внешние входы/выходы, **step-debug**: `StepOnce(StepInfo)`/`ResetStep()`/`step_result()` |
| `node_graph_history.*` | `History` + `Command` (Apply/Revert/MergeWith), `MacroCommand`, `SnapshotCommand`, `GroupCommand` — undo/redo всех правок, включая группы |
| `node_graph_serializer.*` | JSON-сериализация графа, `kGraphFormatVersion`, валидация при загрузке |
| `node_graph_json.*` | JSON DOM (`Json::Value`-подобный) |
| `node_graph_builtins.*` | Встроенные типы: math, string, array (convolve через MKL FFT), io.*, graph.input/output/subgraph |
| `node_graph_ext.*` | Расширенные типы: MKL-ускоренные array.* (vdSin, cblas), string.* (UTF-8, regex, base64, crc32, compress) |
| `node_graph_project_store.*` | `ProjectStore` — проекты на SQLite: создание/настройка/переименование/удаление, графы, пресеты |

### `src/ui/` — UI-фреймворк

#### `core/`

`Window` (HWND + цикл сообщений, таймер `frame_callback_`),
`Application` (синглтон, Run/Shutdown).

#### `canvas/`

`Canvas` — дочерний HWND поверх окна: растр через Skia surface **или**
GPU-путь D3D12+Ganesh (`TryInitGpu`, `GaneshD3DAllocator` — committed
resources, flip-model swapchain, device-removed → откат на растр).
Колбэки ввода/рендера, стек клипов, `DrawImageBgra` — persistent texture.

#### `widgets/` — 80 виджетов

- **NodeGraph:** `node_graph_editor_widget.*` — основной редактор:
  pan/zoom к курсору, привязка к сетке, перетаскивание нод/связей,
  box-select, copy/paste/duplicate, выравнивание/распределение,
  палитра+поиск, контекстные меню (Add Node, Problems, Group/Ungroup,
  Collapse/Expand, Step Graph, Reset Debug), подграфы, миникарта,
  бейджи валидации, автосохранение, инспектор со значениями портов.
  `node_param_host.*` — прозрачный HWND-оверлей с нативными контролами
  параметров внутри нод (EDIT/COMBOBOX/CHECKBOX/выбор файла),
  click-through через `WS_EX_LAYERED`+colorkey.
- **Базовые:** `widget`, `widget_tree`, `button`, `label`, `checkbox`,
  `radio_group`, `select`, `switch`, `toggle`, `slider`, `text_input`,
  `textarea`, `badge`, `card`, `alert`, `dialog`, `context_menu`,
  `breadcrumb`, `pagination`, `progress`, `separator`, `skeleton`,
  `spinner`, `tabs`, `tooltip`, `scroll_area`, `accordion`, `avatar`
- **Специализированные:** `projects_widget` (браузер проектов:
  Select/Config/Duplicate/Delete + поиск), `settings_widget`,
  `fx_rack_widget` + `fx_rack_state` (цепочка эффектов),
  `eq_editor` (кривые EQ), `waveform_widget`, `audio_player_widget`,
  `video_preview_widget`, `chain_map_widget`, `screen_loading_widget`
  (сплэш: проверки БД/проектов/кэша/драйверов)
- **Kit-примитивы:** `kbd`, `marker`, `aspect_ratio`, `button_group`,
  `input_group`, `field`, `item`, `empty`, `collapsible`, `direction`,
  `form`, `attachment`, `bubble`, `message`, `message_scroller`,
  `input_otp`, `hover_card`, `popover`, `dropdown_menu`, `sheet`,
  `drawer`, `resizable`, `carousel`

#### `theme/`, `layout/`, `motion/`, `colorspace/`, `rendering/`, `platform/`, `backends/`

- `theme/` — светлая/тёмная тема на дизайн-токенах (`Theme::Tokens`, `Theme::Color`)
- `layout/geometry.h` — Rect/Point/Size
- `motion/` — `Motion` (easing-анимации), `accel` (**MKL VML** для float-операций)
- `colorspace/` — `ColorSpace`, `ColorManager`, `HdrManager`
- `rendering/renderer.h` — интерфейс рендерера
- `rendering/windows/direct2d_renderer.*` — бэкенд D2D
- `platform/windows/input_translator.*` — Win32-ввод → события
- `debug/` — DevTools: `debug_map_ui.*` (дерево виджетов → XML через libxml2),
  `elements_view.*` (сворачиваемая панель дерева), `dev_tools.*` (окно
  инспектора: вкладки Elements / Render / Stats)
- `xml/xml_ui.*` — декларативные деревья виджетов из XML-разметки (libxml2)
- `backends/flux/` — адаптеры Flux: `FluxRenderer`, `FluxInputAdapter`
- `demo/audio_player_demo.cc` — демо

#### `css/` — CSS-движок (`XER::UI::CSS`, → `YA.DesginCSS.dll`)

Расчёт и инвалидация стилей в стиле Chromium:

- `css_selector.*` — парсер + матчер: tag, `.class`, `#id`, `*`,
  `[attr]`, `[attr=value]`, `:focus`, `:hover`, `:active`,
  `:first/last-child`, `:nth-child(an+b)`; комбинаторы descendant и `>`;
  списки селекторов через запятую
- `rule_set.*` — индексированное хранилище `RuleData` (`std::deque` —
  стабильность указателей): карты ClassRules/IdRules/TagRules/Universal
  по right-most compound; индекс ancestor-класс → `InvalidationSet`
- `selector_checker.*`, `element_rule_collector.*` — сопоставление
- `style_resolver.*`, `computed_style.h` — каскад:
  UA < user < author → специфичность → порядок в источнике
- `invalidation_set.*`, `pending_invalidations.h`,
  `style_invalidator.*` — карта отложенных инвалидаций, DFS-инвалидация,
  флаги пересчёта себя/потомков
- `css_node.h`, `tree_scope.h`, `document_style.*` — адаптер DOM
  (`CSSElement`/`CSSNode` — биты состояния) и
  `UpdateStyleAndLayoutTree` — push pending → пересчёт в порядке документа
- `stylesheet_parser.*` — текстовый CSS → `StyleSheet`/`StyleRule`

### `src/media/` — медиа-конвейер (`XER::Media::*`)

Модульный стек воспроизведения/обработки, разбитый на `YA.*.dll`:

| Подмодуль | Назначение |
|---|---|
| `base/` | `SeekableBuffer`, лог медиа, утилиты времени |
| `demuxer/` | Интерфейс `DataSource`, `FileDataSource`,
  `UrlDataSource` (HTTP range-запросы через сетевой слой),
  `BufferedDataSource` (байтовое окно с опережающим чтением),
  `MediaSource`/`ChunkDemuxer` (append/EOS в стиле MSE),
  мост к демуксеру FFmpeg |
| `decoders/` | Интерфейсы аудио/видео декодеров + адаптеры FFmpeg
  (универсальный декодер — в `YA.MediaDecoderUniversal.dll`) |
| `renderers/` | `AudioRendererImpl`, `VideoRendererImpl`,
  синхронизация по аудио-часам, композитор кадров |
| `sinks/` | `WasapiOutputStream`, `FrameCacheSink`,
  pull-модель на потоке устройства |
| `muxers/` | Муксеры контейнеров (семейство EBML/MP4) |
| `player/` | `MediaPlayer`/`MediaPlayerImpl` — оркестрация конвейера |
| `session/`, `service/` | Медиа-сессия и внепроцессный сервис |
| `router/` | Маршрутизация медиа между источниками, рендерерами и приёмниками |
| `gpu/`, `capture/`, `cdm/`, `filters/`, `metrics/` | GPU-интеграция,
  устройства захвата, заглушки CDM, граф фильтров, метрики |

### `src/flux/` — минимальный фреймворк (ядро LSUI)

| Подмодуль | Файлы | Назначение |
|---|---|---|
| `core/` | `flux_core`, `flux_config`, `flux_context`, `logger`, `module_manager` | Инициализация, конфиг, логирование, модули |
| `terminal/` | `terminal_emulator`, `terminal_buffer`, `terminal_ansi_parser`, `terminal_colors`, `terminal_styles`, `terminal_extended`, `terminal_input_handler`, `terminal_interface`, `terminal_manager`, `terminal_output_renderer`, `terminal_size_detector`, `terminal_window` | Полный ANSI-терминал: буфер, парсер, стили, fit/center/rule, snapshot |
| `rendering/` | `renderer`, `render_context`, `render_pipeline`, `frame_buffer` | Конвейер рендеринга |
| `input/` | `input_manager`, `key_binding_manager`, `command_parser` | Ввод и привязки клавиш |
| `history/` | `action_history`, `command_history` | История действий/команд |

### `src/lsui/` — lsui.exe: UI на Lua

| Файл | Описание |
|---|---|
| `lsui.cc/.h` | `UiWindow` — окно на Canvas, Lua API: `ui.window{}`, `on_tick/on_mouse/on_key`, immediate-mode отрисовка (`rect/text/button`...), `watch()` на чистом Lua, crash-фильтр в main |
| `lsui_term.cc` | Модуль `term.*` поверх `flux/terminal`: print/println/write/write_err, clear/snapshot/text, size, fit/center/rule, fg/bg/style/reset |
| `main.cc` | Точка входа: состояние Lua, `kLuaHelpers` — **require в стиле Node.js**: `./rel`, `../rel`, подъём по `node_modules`, `LSUI_PATH`, `mod.lua`/`mod/init.lua`, `module.exports`/`exports`, частичные экспорты при циклических require, `require.resolve`, `__dirname`/`__filename`/`module`/`exports` |

### `src/wrapper/` — мосты к внешним библиотекам (`engine::bridge::*`)

| Модуль | Файлы | Назначение |
|---|---|---|
| `skia/` | `skia_engine_bridge` + `_codec_extra`, `_fx`, `_image_filter`, `_lottie`, `_m44`, `_pdf`, `_shader`, `_svg` | Skia API: кодеки изображений, цветовые фильтры, шейдеры, SVG/Lottie/PDF, матрицы |
| `ffmpeg/` | `ffmpeg_engine_bridge`, `ffmpeg_video_decoder` | FFmpeg: декодер с аудио-ответвлением (`EnableAudioTap`, `ReadAudioFrames` на отдельном потоке, очередь пакетов под мьютексом) |
| `cuda/` | `cuda_engine_bridge` | Мост CUDA runtime |
| `cudnn/` | `cudnn_engine_bridge` | Мост cuDNN |
| `angle/` | `angle_engine_bridge` | ANGLE (OpenGL ES → D3D) |
| `opencv/` | `opencv_engine_bridge` | Мост OpenCV |

### `src/content/` — контент-модули (`XER::Content::*`)

#### `network/` — сетевой стек (~100 файлов, неймспейс `XER::network::*`)

| Подмодуль | Файлы | Назначение |
|---|---|---|
| `socket/` | `tcp_client_socket`, `udp_client_socket`, `ssl_client_socket`, `socket_client`, `socket_server`, `socket_stream`, `socket_proxy`, `network_selector`, `net_log` + `platform/{windows,posix}/socket_platform_*` | Сокеты: TCP/UDP/SSL, селектор, прокси, платформенные абстракции |
| `ssl/` | `ssl_context`, `ssl_session_cache` | SSL-контекст и кэш сессий |
| `cert/` | `cert_database`, `cert_storage`, `cert_transparency`, `cert_verifier`, `cert_verify_result`, `nss_certificate`, `scoped_nss_types` | Сертификаты X.509/NSS, CT-логи, верификация |
| `dns/` | `dns_client`, `dns_resolver`, `dns_config_service`, `dns_record_type`, `host_resolver`, `host_resolver_impl` | DNS-резолвер |
| `http/` | `http_auth`, `http_cache`, `http_network_delegate`, `http_network_session`, `http_request/response_headers`, `http_server_properties`, `http_stream`, `http_stream_factory`, `http_sync_request`, `http_transaction`, `http_util` | Клиент HTTP/1.1: сессии, транзакции, кэш, аутентификация |
| `quic/` | `quic_connection`, `quic_crypto_stream`, `quic_network_delegate/session`, `quic_packet`, `quic_protocol`, `quic_server_id`, `quic_stream`, `quic_version_manager` | Протокол QUIC |
| `spdy/` | `spdy_framer`, `spdy_network_delegate`, `spdy_protocol`, `spdy_session`, `spdy_stream` | Протокол SPDY |
| `websocket/` | `websocket_frame`, `websocket_handshake_stream`, `websocket_network_delegate`, `websocket_stream`, `websocket_event_interface` | Клиент WebSocket (RFC6455) |
| `ftp/` | `ftp_ftp_request`, `ftp_ftp_stream`, `ftp_ftp_network_delegate` | Клиент FTP |
| `url/` | `url_request`, `url_request_context(+getter)`, `url_request_intercepting_job`, `url_request_job`, `url_request_util`, `url_util` | URL-запросы и парсинг |
| `proxy/` | `proxy_config`, `proxy_info`, `proxy_resolver(+factory)`, `proxy_service`, `socks_client_socket` | Прокси HTTP/SOCKS |
| `transport/` | `transport_client_socket`, `transport_server_socket`, `transport_io_buffer`, `transport_network_delegate/session` | Транспортный слой |
| `core/` | `network_context`, `network_service`, `network_service_delegate`, `network_service_util`, `network_task_runner` | Сетевой сервис и контекст |
| `async/` | `async_operation_manager` + `iocp/iocp_completion_port`, `posix/epoll_async_socket` | Асинхронные операции: IOCP (Windows) / epoll (POSIX) |
| `cache/` | `network_cache_manager` | Кэш HTTP-ответов |
| `logging/` | `network_logger` | Логирование сети |
| `network_util/` | `network_util` | Утилиты |

#### Прочие контент-модули

| Модуль | Файлы | Назначение |
|---|---|---|
| `video/` | `video_session` (фоновый воркер декодирования), `video_decoder`, `video_engine`, `video_audio_player` (потоковое WASAPI-воспроизведение из общего декодера), `video_frame(+cache)`, `video_info`, `video_command_buffer` | Видео: декодер FFmpeg, play/pause/seek, аудиодорожка из того же контейнера |
| `s3/` | `s3_client`, `s3_signer` (AWS Signature V4), `s3_types` | S3-совместимое хранилище |
| `hex/` | `hex.h` (+`detail`) | HEX-кодирование/декодирование байт |
| `clipboard/` | `clipboard` | Буфер обмена |
| `audio/` | `audio_player` | Контентный аудиоплеер |

### `src/sql/` — SQL-движок (`XER::Engine::SQL::*`)

| Подмодуль | Файлы | Назначение |
|---|---|---|
| `storage/` | `sqlite_database` | Обёртка SQLite: open/exec/prepare/transaction |
| `modern/` | `query_builder`, `query` | Fluent-построитель запросов (`SELECT/INSERT/UPDATE/DELETE`) |
| `orm/` | `table_schema` | Схемы таблиц для ORM |
| `content/` | `value`, `row` | `Value` (типы SQL), `Row` |
| `plus/` | `transaction` | Транзакции |

### `src/cache/` — кэш-хранилище (`XER::Engine::Cache`)

`CacheManager` (синглтон, корни `Persistent()`/`Temporary()`),
`PersistentStorage`, `TemporaryStorage`, `CacheStorage`, `CacheEntry`,
`CacheConfiguration`, `CacheConstants`, `CacheEncryptionHandler`,
`CacheScanAndLock`, `CacheUtils`, `SharedMemoryChannel`
(разделяемая память для межпроцессного аудио).

### `src/helper/` — утилиты (`XER::Engine::Helper`)

`string_utils` + `helper/string` (строковые хелперы, UTF-8),
`buffer` (Buffer), `dict` (Dict), `file_diff` (diff),
`compression_codec` (zlib), `jsoncpp_helper` (мост Json::Value),
`tool_to` (Tool::ToString и др.), `span_reader`/`span_writer`
(bounds-checked LE/BE чтение/запись), `raw_ptr` (`RAWPtr<T>`,
`RawSpan<T>` — невладеющие обёртки).

### `src/lunca/` — Lua-сеть (`common/`)

`packet` (упаковка пакетов), `udp_socket` — минимальный UDP для Lua.

### Корневые файлы

| Файл | Описание |
|---|---|
| `main.cc` | Точка входа: установка crash-handler → App::Main |
| `app_command.h/.cc` | `XER::AppCommand` — парсер CLI-команд (в `namespace XER`) |
| `engine_params.h/.cc` | `XER::EngineParams` — конфиг рантайма из CLI/окружения |
| `audio_config_helper.h/.cc` | Применение audio-JSON конфига к командам |
| `json_config_helper.h/.cc` | Хелперы JSON-конфигов |
| `tools/EngineScanVST3/main.cc` | Утилита сканирования VST3-плагинов (путь, имя, параметры) |

---

## <a id="node_graph"></a>NodeGraph: модель, исполнитель, история

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

### Executor — запуск, инкрементальность, step-debug

```cpp
XER::NodeGraph::Executor ex(graph);

// Полный прогон
XER::NodeGraph::ExecutionResult res = ex.Run();
// res.ok / res.evaluated / res.cached / res.node_errors / res.logs

// Пошаговое исполнение (один уровень за вызов)
XER::NodeGraph::Executor::StepInfo step;
while (ex.StepOnce(&step)) {
    // step.level, step.total_levels, step.nodes — ноды этого шага
    // значения портов уже в кэше:
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
hist.Undo();   // откатывает весь макрос
hist.Redo();
```

### Сериализация и проекты

```cpp
#include "node_graph/node_graph_serializer.h"
#include "node_graph/node_graph_project_store.h"

std::string json = XER::NodeGraph::SerializeGraph(graph);
XER::NodeGraph::Graph loaded(registry);
XER::NodeGraph::DeserializeGraph(json, &loaded);   // проверка версии

// Проекты на SQLite
XER::NodeGraph::ProjectStore store(db_path);
store.CreateProject("My Project", &err);
store.SaveGraph(project_id, "main", graph);
```

---

## <a id="ui"></a>UI: canvas, виджеты, редактор NodeGraph

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
    return false;  // true = событие поглощено
});
```

GPU-путь: D3D12 + Ganesh, собственный `GaneshD3DAllocator`
(committed resources), flip-model swapchain, откат на растр
при device-removed.

### NodeGraphEditorWidget

```cpp
auto editor = std::make_shared<XER::UI::Widgets::NodeGraphEditorWidget>(
    hwnd, theme_hook, project_id);
editor->SetHostVisible(true);          // canvas + оверлей параметров
editor->SetPluginEditorHandler(...);   // HWND редактора VST3
editor->SetBrowserBackHook(...);       // кнопка "назад"
window->SetContent(editor);
```

Возможности: pan/zoom к курсору, привязка к сетке, drag нод/связей,
box-select, мультивыбор, copy/paste/duplicate, выравнивание/распределение,
поиск нод (палитра), контекстные меню, подграфы, миникарта, группы
(Group/Ungroup Selection), сворачивание нод (заголовок+порты),
бейджи валидации + меню Problems, step-debug (Step Graph/Reset Debug),
автосохранение, инспектор со значениями портов.

### Виджеты

Все виджеты — immediate-mode поверх `Renderer` + `WidgetTree`:
`Button`, `Label`, `Checkbox`, `Select`, `Slider`, `TextInput`,
`Dialog`, `ContextMenu`, `Tabs`, `ScrollArea`, `ProjectsWidget`,
`SettingsWidget`, `FxRackWidget`, `EqEditor`, `WaveformWidget`,
`AudioPlayerWidget`, `VideoPreviewWidget`, `NodeParamHost` и др.

Тема — дизайн-токены: `Theme::Tokens` (light/dark),
`Theme::Color{r,g,b,a}`, `Mix`, `AccentC`, `DangerC`, `WarnC`.

---

## <a id="flux"></a>Flux: ядро, терминал, рендерер

Flux — минимальный фреймворк, на котором построены LSUI (и частично UI).

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

## <a id="lsui"></a>LSUI — UI на Lua

`lsui.exe script.lua` — запускает Lua-скрипт с окнами UI.

```lua
-- demo.lua
local ui = require("lsui")

local win = ui.window({
    title = "My App",
    width = 400, height = 300,
    on_tick = function(w, dt) end,
})

win:button({ x = 10, y = 10, w = 100, h = 30, label = "Click" })

ui.watch(1.0, function()          -- каждую секунду
    print("tick")
end)

-- require в стиле Node.js:
local util = require("./lib/util")        -- относительно файла
local pkg  = require("mylib")             -- node_modules walk-up / LSUI_PATH
```

Модуль терминала:

```lua
local term = require("term")
term.println("Hello", { fg = "bright_blue", bold = true })
term.rule("=")
local w, h = term.size()
local snap = term.snapshot()
```

---

## <a id="audio"></a>Аудио: DSP, синтезатор, плагины, кодеки

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

### Хостинг VST3/CLAP

```cpp
#include "audio/plugin_wrappers/vst3_external_host.h"
#include "audio/plugin_wrappers/clap_external_plugin_host.h"

XER::Engine::Audio::Plugin::Vst3ExternalHost host;
host.Load("C:/Plugins/MySynth.vst3", 44100.0, 512, &err);
host.SetParameter(0, 0.75f);
host.Process(in_block, out_block, 512);
```

### Аудио-ноды NodeGraph (`app/node_graph_app_nodes`)

```cpp
// Регистрация типов: plugin.vst3, plugin.vst3_view, lua.buffer,
// fx.<algo>, net.websocket_*, s3.*, io.file_*, audio.*
// Состояния VST3/Lua живут в g_vst3_hosts / g_lua_states под мьютексом.
```

---

## <a id="network"></a>Сеть: сокеты, HTTP, QUIC, WebSocket, S3

```cpp
#include "content/network/socket/tcp_client_socket.h"
#include "content/network/websocket/websocket_stream.h"
#include "content/network/http/http_sync_request.h"

// TCP
XER::Content::Network::TcpClientSocket sock;
sock.Connect("example.com", 80, &err);

// WebSocket (нода NodeGraph net.websocket_send)
auto ws = std::make_unique<XER::network::websocket::WebSocketStream>(
    std::move(sock));
ws->SendText("hello", &err);

// Синхронный HTTP
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

## <a id="video"></a>Видео: декодер FFmpeg, сессии, аудиоплеер

```cpp
#include "content/video/video_session.h"
#include "content/video/video_audio_player.h"

XER::Content::Video::VideoSession session;
session.Open("video.mp4", &err);     // фоновый воркер декодирования
session.Play(); session.Pause(); session.Seek(10.0);

XER::Content::Video::VideoAudioPlayer player;
player.Attach(session.GetAvDecoder());         // тот же контейнер!
player.Play();
```

`VideoDecoder` — обёртка FFmpeg: видеокадры на одном потоке,
`ReadAudioFrames` на втором (аудио-пакеты через очередь под мьютексом),
`EnableAudioTap(true)` — аудио из того же открытого контейнера.

---

## <a id="sql"></a>SQL: ORM, построитель запросов, SQLite

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

## <a id="async_io"></a>AsyncIO: потоки, память, аппаратура

```cpp
#include "async_io/io_thread_pool.h"
#include "async_io/hardware_abstraction/system_info.h"

auto& pool = XER::AsyncIO::IO::Async::IOThreadPool::GetSharedInstance();
pool.Enqueue([] { /* задача воркера */ });
pool.WaitAll();                      // ожидание очереди

// Системная информация (асинхронный снимок → JSON)
auto fut = XER::AsyncIO::HardwareAbstraction::SystemInfoProvider::Instance()
    .GetSnapshotAsync();
```

`HwTempSensor` — мониторинг температур с callback алертов;
`HwPowerMonitor` — мощность RAPL (Linux) / заглушка (Windows);
`NvmlLoader` — NVIDIA NVML.

---

## <a id="misc"></a>Кэш, Helper, обёртки

```cpp
#include "cache/cache_manager.h"
#include "helper/string_utils.h"

// Кэш: %TEMP%/XER/cache — persistent + temporary корни
auto& cache = XER::Engine::Cache::CacheManager::Instance();
auto root = cache.Persistent().root();

// Строковые утилиты
XER::Engine::Helper::String::ToUpper("hello");   // "HELLO"
XER::Engine::Helper::String::Split("a,b,c", ',');

// HEX
XER::Content::Hex::Encode(data.data(), data.size());
XER::Content::Hex::Decode("deadbeef");
```

Обёртки (`engine::bridge::*` → теперь `XER::engine::bridge::*`):
Skia (полный графический API + кодеки + SVG/Lottie/PDF), FFmpeg,
CUDA, cuDNN, ANGLE, OpenCV — тонкие C++-обёртки над C API.

---

## <a id="code-examples"></a>Примеры кода

### 1. NodeGraph: создать граф, добавить ноды, связать, сохранить

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

// Сохранение
std::string json = SerializeGraph(graph);
```

### 2. NodeGraph: группы + сворачивание + валидация

```cpp
// Группа через History (отменяемая)
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

// Сворачивание на стороне редактора: Level::collapsed_nodes — set<NodeId>
// Свёрнутая нода = заголовок + порты (без строк параметров), связи работают.
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

### 4. Плагин VST3 в NodeGraph

```lua
-- В NodeGraph добавьте ноду plugin.vst3, параметры:
--   path = "C:/Plugins/Serum.vst3"
--   sampleRate = 44100
--   p0..p7 = нормализованные параметры (0..1)
-- После исполнения графа редактор откроет GUI плагина автоматически.
```

### 5. Видео FFmpeg + аудио

```cpp
XER::Content::Video::VideoSession vs;
vs.Open("movie.mkv");

// Аудио из того же декодера:
XER::Content::Video::VideoAudioPlayer ap;
ap.AttachDecoder(vs.SharedDecoder());
ap.Play();
vs.Play();
// SyncTo() держит позиции синхронно (дрейф > 300мс → seek).
```

### 6. Пул потоков AsyncIO + информация о железе

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
// %TEMP%/XER/cache/crashes/<timestamp>/, авто-перезапуск (--crash-depth<=2)
// stackscan в crash.log показывает адреса возврата без frame pointers.
```

---

## <a id="node-addon"></a>Аддон для Node.js

`addon/` собирает `xer.node` — нативный модуль NAN/V8, экспортирующий
API XER в Node.js как ES-классы (по классу-обёртке на файл,
сгруппированные по подсистемам).

```js
const xer = require('./addon/index.js');
const { audio, UI } = xer;
```

### API класса Audio (`xer.audio`)

Верхний уровень: `audio.info(path)`, `audio.convert(in, out, opts?)`,
`audio.Buffer` (декодированный PCM — единственный вход, который принимает
`audio.Player`), `audio.Player` (воспроизведение WASAPI).

Пространства подсистем — каждый нативный класс является JS-классом:

| Неймспейс | Классы / функции |
|---|---|
| `audio.dsp` | `Biquad`, `FFT`, `STFT`, `DelayLine`, `Convolution`, `PhaseVocoder`, `PitchShifter`, `SpectralShaper`, `FIRBank`, `IIRBank`; функции `Windowing` (`Hann`, `Hamming`, `Blackman`, `SqrtHann`, `Apply`, `Describe`) |
| `audio.fx` | `Compressor`, `Expander`, `Gate`, `Limiter`, `Bitcrush`, `Tube`, `GraphicEQ`, `ParametricEQ`, `Chorus`, `Tremolo`, `Vibrato`, `RingMod`, `Flanger`, `Phaser`, `AutoTune`, `ReverbAlgo`, `ReverbConv`; `audio.fx.custom` — `List()`, `RenderExample(rate, samples)`, `Run(pkg, rate, samples)`, `RunInterleaved(pkg, rate, samples, channels)` |
| `audio.analysis` | `BeatTracker`, `HarmonicContent`, `LoudnessMeter`, `OnsetDetector`, `PitchEstimator`, `RmsPeak`, `Spectrogram`, `FFTAnalyzer`, `PitchDetector` |
| `audio.codec` | `WavPcm`, `WavFloat`, `Flac`, `Mp3`, `Ogg`, `Aac`, `Id3`, `FileStreamer` |
| `audio.midi` | `Parser`, `Sequencer`, `Clock`, `Mapping`, `Dispatcher` (JS-колбэки), `Sysex`, `Automation` |
| `audio.synth` | `Adsr`, `MultistageEnv`, `Filter`, `Lfo`, `ModMatrix`, `OscAnalog`, `OscFm`, `OscNoise`, `OscWavetable`, `Polyphony`, `VoiceManager` |
| `audio.core` | `Engine`, `EngineHost`, `ClockSync`, `Fifo`, `Interleave`, `BitDepth`, `Resample`, `NativeBuffer`, `Latency`, `SourceLoader`, `Logger`, `BufferManager`, `CacheManager`, `AsyncManager`; функции приоритета потоков (`SetCurrentThreadPriority`, `SetThreadAffinity`, `GetOptimalCore`, `GetPriorityName`, …) |
| `audio.plugin` | `ParamBridge`, `BuiltinHost`, `Vst3Host`, `ClapHost`, `AuHost`, `Vst3External`, `ClapExternal`; `ScanVst3Plugins(roots?)`, `DefaultVst3SearchRoots()` |

```js
const buf = audio.Buffer.FromFile('song.mp3');        // или new audio.Buffer({...})
const player = new audio.Player();
player.Load(buf);                                     // ТОЛЬКО Buffer

const comp = new audio.fx.Compressor();
comp.SetThresholdDb(-20); comp.SetRatio(4);
const out = comp.ProcessBlock(pcm);                   // Float32Array -> Float32Array

const src = new audio.core.SourceLoader();
const decoded = src.Load({ path: 'in.wav', targetSampleRate: 48000 });

const pkg = { label: 'my-chain', nodes: [
  { label: 'comp', backend: 'builtin', algorithm: 'Compressor' }]};
const res = audio.fx.custom.Run(pkg, 44100, pcm);     // {output, report}
```

Соглашения: аудио-буферы — `Float32Array` (interleaved для многоканала),
байтовые потоки — `Uint8Array`. Методы с `(samples, channels)`
трактуют массив как interleaved frames×channels; моно-методы DSP/FX
принимают длину массива как число кадров.

### Дизайн UI (`xer.UI`)

`UI.Application`, `UI.Window`, подклассы `UI.Widget` плюс ~60 виджетов
с темами: примитивы (`Button`, `Label`, `TextInput`, `Slider`, …),
оверлеи (`Popover`, `DropdownMenu`, `HoverCard`, `Sheet`, `Drawer`,
`Dialog`, `ContextMenu`, `Command`), отображение данных (`Table`,
`Chart`, `Calendar`, `Sidebar`), ввод (`Combobox`, `InputOTP`,
`Select`, …).

#### DevTools (`src/ui/debug/`)

Два инспектора для разработчика, построенные на дереве виджетов:

- **`win.OpenDevTools()`** (аддон) / `Debug::DevTools::Inspect(window)`
  (C++) — открывает отдельное окно DevTools с тремя вкладками:
  **Elements** (сворачиваемое дерево виджетов с типом, границами и
  состоянием, плюс панель деталей выбранного поддерева), **Render**
  (живые тайминги кадра `GetRenderStats()`, покрытие damage,
  последняя ошибка отрисовки) и **Stats** (число виджетов по типам,
  итоги видимости/доступности, макс. глубина). Автообновление ~1 с;
  окно живёт до закрытия.
- **`widget.DumpXml()`** (аддон) / `Debug::DebugMapUI::Dump(widget)`
  (C++) — сериализует поддерево виджетов в XML-документ через libxml2:
  элемент на виджет (`<Button type="XER::UI::Widgets::Button" …>`)
  с геометрией (`x/y/w/h`), состоянием (`visible/shown/enabled/focused/
  hovered/focusable/clips/dir`) и атрибутами `text`/`tooltip`
  (`Widget::GetDebugText()` даёт текст для Label/Button).
  XML-экранирование и защита от глубины/циклов встроены.

#### XML-разметка UI (`src/ui/xml/`)

`Xml::Load` / `Xml::LoadFile` (C++) и `UI.parseXml` / `UI.parseXmlFile`
(аддон) инстанцируют деревья виджетов из XML — «HTML, но XML»: имена
элементов мапятся на классы виджетов, атрибуты — на свойства,
вложенные элементы — на детей:

```xml
<ScrollArea><Form columns="2" gap="8">
  <Label text="Name"/>
  <TextInput placeholder="Enter name"/>
  <Button text="Save" style="primary"/>
</Form></ScrollArea>
```

Общие атрибуты на каждом элементе: `x`/`y`/`width`/`height`
(границы в DIP), `visible`, `enabled`, `tooltip`, `dir`. Контейнеры
интерпретируют детей по-особому (`Tabs` принимает `Tab`, `Table` —
`Column`/`Row`, `ScrollArea` — одного ребёнка-контент, … — см.
`xml_ui.cc`).

Загрузчик никогда не бросает: возвращает `{ ok, widget, warnings, error }`.
Неизвестные элементы пропускаются с предупреждением `line N:`; неизвестный
или некорректный корень — фатально (`ok === false`). Проверяйте `warnings`
на опечатки — `UI.xmlSupportedTags()` перечисляет все ~50 поддерживаемых
тегов для инструментов. Требуется libxml2 (порт vcpkg `libxml2`; без него
API возвращает описательную `error` вместо дерева).

---

### OpenAI-совместимый сервер (`xer.OpenAI`)

`xer.OpenAI.Server` оборачивает `Content::OpenAI::OpenAIServer` — локальный
OpenAI-совместимый эндпоинт (`/v1/chat/completions`,
`/v1/images/generations`, `/v1/audio/*`, `/v1/models`), работающий на
HTTP-стеке `content/network`. JS-обработчики регистрируются на том же
роутере и автоматически маршаллятся на главный поток Node:

```js
const srv = new xer.OpenAI.Server({ host: '127.0.0.1', port: 8080 });
srv.on('/hello', (req, res) => res.json({ ok: true }));
srv.on('POST', '/echo', (req, res) => res.json({ body: req.body }));
srv.onHandleCompletion((req, res) => res.json({ choices: [] }));
srv.onHandlerKey((req, verify) => verify(true));   // опциональный auth-гейт
srv.start();
```

Объекты запроса экспонируют `method/path/query/headers/body/json/rawBody`;
ответы — `status()/setHeader()/json()/send()/end()`.

---

## Модульная упаковка YA.*.dll

Статические библиотеки проекта упакованы в общие модули `YA.*.dll`
(`cmake/YaDll.cmake` + генерируемые `.def`-экспорты,
`cmake/GenYaDef.cmake`). Исполняемые файлы линкуют YA-DLL неявно;
сторонние зависимости встроены внутрь YA-модулей как статические архивы,
поэтому бинарники работают без MinGW/MSYS2-рантайма в `PATH`. Примеры:
`YA.HelperBase`, `YA.DesginCSS`, `YA.MediaPipeline`, `YA.MediaAudio`,
`YA.MediaDecoderUniversal` (весь статический closure FFmpeg),
`YA.LibXml2`, `YA.AudioCore`, `YA.VideoCore`.

## Статус и известные проблемы

- **Миграция на неймспейс `XER::`** — завершена по всей кодовой базе
  (~230 файлов), полная сборка проверена.
- **GPU**: D3D12+Ganesh работает; device-removed → откат на растр;
  `gpu_trace.log` в `%TEMP%/XER/`.
- **Краши**: stackscan в crash_handler для диагностики rip=0/AV.
- **Тесты**: `XER_UI_Tests.exe` (UI + FFmpeg), `XER_Media_Smoke.exe` —
  21 проверка: конвейер demux/decode/render, матчинг/каскад CSS,
  инвалидация стилей, парсер stylesheet, `BufferedDataSource`.
- **Delay-load DLL**: ggml/whisper/shaderc/vulkan грузятся лениво из
  `YA.MediaDecoderUniversal.dll`; бинарник стартует без них и подгружает
  при первом использовании.
