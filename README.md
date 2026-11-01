# XER

XER is a C++ and JavaScript-oriented runtime platform that combines native compilation, runtime orchestration, and multimedia-focused subsystem integrations.

The project is designed around a modular architecture: JavaScript-driven workflows coordinate runtime operations, while native components handle compilation, I/O, networking, audio, and GPU-aware processing where needed.

## Highlights

- JavaScript-first orchestration through `EngineBuilder` and FlowScript-based execution patterns
- Dynamic native compilation with Clang/LLVM integration via `RuntimeLive`
- Asynchronous I/O and buffer management for high-throughput workloads
- Audio processing and DSP-oriented runtime capabilities
- Network and content subsystems for HTTP, sockets, and protocol integration
- AI and ML-oriented model infrastructure with optional backend integrations
- Terminal, UI, and runtime helper components under `src/flux` and related modules

## Repository structure

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
└── project metadata and dependency configuration
```

### Source tree

```text
src/
├── async_io/            # async file I/O, task queues, buffer management
├── audio/               # audio engine and DSP-oriented subsystems
├── cache/               # caching helpers and persistence utilities
├── content/
│   ├── ai/              # model-building and ML infrastructure
│   ├── network/         # HTTP, sockets, DNS, and protocol abstractions
│   └── openai/          # OpenAI integration layer
├── flux/                # terminal, rendering, and runtime helpers
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
- Optional vcpkg setup for dependencies

### Typical configure/build flow

```bash
cmake --preset default
cmake --build --preset default
```

Additional presets may be used for specialized tasks such as V8 integration, OpenCV support, and validation workflows.

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

- [demo_app/README.md](demo_app/README.md) — demo runners and smoke tests
- [src/README.md](src/README.md) — core source layout
- [src/async_io/README.md](src/async_io/README.md) — asynchronous I/O subsystem
- [src/audio/README.md](src/audio/README.md) — audio and DSP subsystem
- [src/cache/README.md](src/cache/README.md) — cache subsystem
- [src/content/network/README.md](src/content/network/README.md) — network stack overview
- [src/content/ai/README.md](src/content/ai/README.md) — AI and ML framework overview
- [src/content/openai/README.md](src/content/openai/README.md) — OpenAI integration overview
- [src/flux/README.md](src/flux/README.md) — Flux terminal/runtime framework

## Status

This project contains a broad set of runtime and subsystem scaffolding. Some components are mature, while others remain experimental or actively evolving. The repository is organized around modular building blocks rather than a single standalone application.

## License

See the repository root for the project license and licensing details.
