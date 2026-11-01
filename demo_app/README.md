# XER

XER is a C++/V8-based runtime and build environment that combines JavaScript-driven orchestration with native compilation and multimedia-oriented subsystems.

The project is designed around a simple idea: use JavaScript to describe build or runtime workflows, while native components handle compilation, I/O, networking, audio, and GPU-aware processing when needed.

## Highlights

- JavaScript-first orchestration through `EngineBuilder` and FlowScript-style runtime flows
- Dynamic native compilation with Clang/LLVM integration via `RuntimeLive`
- Async I/O and buffer management for data-heavy workloads
- Audio processing and DSP-oriented framework pieces
- Network and content subsystems for HTTP, sockets, and service integration
- AI/ML-oriented model infrastructure and optional integrations
- Terminal and UI-oriented runtime components under `src/flux` and related modules

## Repository layout

```text
XER/
├── README.md
├── AGENTS.md
├── CMakeLists.txt
├── CMakePresets.json
├── build.bat
├── cmake/
├── demo_app/
├── docs/
├── plugins/
├── src/
├── tests/
└── vcpkg configuration and dependency metadata
```

### Source tree

```text
src/
├── async_io/            # async file I/O, task queues, memory pools
├── audio/               # audio engine and DSP-oriented modules
├── cache/               # caching strategies and storage helpers
├── content/
│   ├── ai/              # ML and model-building framework pieces
│   ├── network/         # HTTP, sockets, DNS, protocol abstractions
│   └── openai/          # OpenAI integration layer
├── flux/                # terminal/UI framework and runtime helpers
├── modules/             # JavaScript modules and runtime bindings
├── app_command.*
├── CMakeLists.txt
├── main.cc
└── README.md
```

## Build

### Prerequisites

- CMake 3.21+
- Git
- A modern C/C++ toolchain
- Optional vcpkg installation for dependencies

### Typical configure/build flow

```bash
cmake --preset default
cmake --build --preset default
```

Additional presets may be used for specialized scenarios such as V8, OpenCV, and xbuild-related validation.

## Quick start

Run the multimedia bridge demo:

```bash
./build/EngineBuilder run demo_app/bridge_multimedia.js
```

Run a smoke suite:

```bash
./out/build/default/EngineBuilder run demo_app/modules_smoke.js
```

## Documentation map

- [demo_app/README.md](demo_app/README.md) — demo runner and smoke tests
- [src/README.md](src/README.md) — core source layout
- [src/async_io/README.md](src/async_io/README.md) — asynchronous I/O subsystem
- [src/audio/README.md](src/audio/README.md) — audio and DSP subsystem
- [src/cache/README.md](src/cache/README.md) — cache subsystem
- [src/content/network/README.md](src/content/network/README.md) — network stack overview
- [src/content/ai/README.md](src/content/ai/README.md) — AI and ML framework overview
- [src/content/openai/README.md](src/content/openai/README.md) — OpenAI integration overview
- [src/flux/README.md](src/flux/README.md) — Flux terminal/runtime framework

## Status

This project contains a broad set of runtime and subsystem scaffolding. Some areas are mature, others are experimental or in active development. The repository is organized around modular building blocks rather than a single standalone application.

## License

See the project license in the repository root for licensing details.
