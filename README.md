# AudioSourceLoader Modernization (2024)


## DSP/Effects: SIMD, Async, Zero-Copy, Memory Stats

- **SIMD/Parallelism:** Все основные DSP/эффекты используют AVX2/SSE2 и многопоточность для ускорения обработки аудиоблоков.
- **Zero-Copy:** Поддержка zero-copy для всех ProcessBlock/ProcessSample — если вход и выход совпадают, копий не происходит.
- **Async API:** Все эффекты и фильтры поддерживают асинхронный вызов через std::future (например, ProcessBlockAsync).
- **Memory Stats:** Для аудиомодулей реализован метод GetMemoryStats() для отчёта по памяти.
- **Unit/Smoke-тесты:** См. tests/dsp_memory_async_zero_copy_test.cc — тестируются zero-copy, async, memory stats, корректность SIMD путей.


- **Strict MP3-only support**: Loader enforces MP3 as primary format, with fallback stub for other formats.
- **Error handling**: Uses `AudioLoaderError` enum and error category for robust error reporting.
- **Async API**: Supports `LoadAsync` and `std::future` for non-blocking decode/process.
- **SIMD/multithreading**: Interleaving/mixing accelerated with SSE2/AVX2 and thread pool.
- **Profiling**: `ScopedTimer` for timing decode and processing steps.
- **MP3 fallback**: FFmpeg primary, planned mpg123 fallback (stub present).
- **Self-test/unit test**: `SelfTest()` method for loader validation and CI.
- **CMake dependency detection**: system packages and pkg-config are detected automatically.
- **Deduplication**: Structure simplified, code duplication removed, error handling unified.

## Usage

```cpp
#include "audio_core/audio_source_loader.h"
AudioSourceLoadOptions opts;
opts.input_path = "file.mp3";
AudioSourceBuffer buf;
std::string err;
auto code = AudioSourceLoader::LoadWithCode(opts, &buf, &err);
if (code != AudioLoaderError::OK) { /* handle error */ }
```

## Self-Test

```cpp
std::string report;
bool ok = AudioSourceLoader::SelfTest(&report);
std::cout << report;
```

## CMake Integration

Detects system FFmpeg and FFTW through pkg-config or CMake Find-modules. See the build instructions for optional features.

## TODO

- Implement full mpg123 fallback for MP3 decode
- Add more unit tests (valid MP3, WAV, error cases)
- Integrate async decode in effect chains
- Advanced error propagation and profiling
# XER 🚀

XER is a high-performance C++/V8 runtime builder that executes JavaScript project configurations and compiles native code dynamically through `RuntimeLive`. It provides a powerful bridge between JavaScript and advanced C++ multimedia/GPU frameworks.

## ✨ Key Features

- **JavaScript-Driven Build Flows:** Orchestrate builds easily via `EngineBuilder`.
- **Dynamic Native Compilation:** Real-time C/C++ compilation with robust Clang/LLVM integration via `RuntimeLive`.
- **Multimedia & GPU Bridges:** Built-in wrappers for Skia, FFmpeg, OpenCV, CUDA, cuDNN, and ANGLE.
- **Rich Module System:** Use `ImportModule("...")` to access the filesystem, cryptography, compression, networking, and asynchronous I/O helpers.
- **In-Process Compile Path:** Seamless Clang/LLVM integration during the `RuntimeLive` compile stage.

## 📦 Project Layout

- `src/` — Core runtime engine and compiler bridge.
- `src/modules/` — Standard JavaScript modules available for import.
- `src/async_io/` — Asynchronous I/O primitives for modules and core utilities.
- `demo_app/` — Active multimedia bridge demo configurations and scripts.

## 🛠️ Build Instructions

### Prerequisites
Install CMake, Git, and a supported C/C++ toolchain. With `XER_FETCH_DEPENDENCIES=ON` (the default), XER CMake downloads dependencies into the project-local `.deps` directory and reuses them on subsequent configure runs. Set `XER_DEPS_DIR` to move this cache elsewhere, or set `XER_FETCH_DEPENDENCIES=OFF` to use system packages only.

### Compilation
```bash
# Configure with the platform's default generator
cmake --preset default

# Build
cmake --build --preset default

# Build V8 and the JavaScript runtime without media dependencies
cmake --preset v8
cmake --build --preset v8

# Build the OpenCV bridge and its libraries
cmake --preset opencv
cmake --build --preset opencv
```

The `default` preset enables media support. Core builds fetch Abseil, pthreadpool, FXdiv, and zlib; media support also fetches zstd, Brotli, FFmpeg, and libusb. Enable `ENABLE_OPENCV=ON` to have XER CMake fetch and build OpenCV; `ENABLE_AI=ON` also enables that dependency. OpenCV's optional ADE, IPP IW, ITT, TBB, and bundled FFmpeg downloads are disabled, and its download cache is kept under `.deps`. Enabling `ENABLE_V8=ON` fetches depot_tools and builds a pinned V8 monolith, as well as fetching libuv, LibXml2, libcurl, and OpenSSL. The `v8` preset builds V8 and `XERJavaScript`; building the `XER` executable additionally requires LLVM/Clang CMake packages. V8 builds require Python, Git, Ninja through depot_tools, and the platform's native compiler toolchain. OpenSSL requires Perl plus `make`/`nmake` to build; when those tools are unavailable, the source is still downloaded and an installed OpenSSL package is required for the executable. On Windows, FFmpeg builds from source with MSVC when Bash and GNU Make (for example from MSYS2) are available; otherwise CMake downloads the source and uses an installed FFmpeg package if present. Set `LLVM_DIR` or `Clang_DIR` when LLVM/Clang are outside standard search paths. CUDA Toolkit/cuDNN are also external SDK prerequisites.

`FindLibNode.cmake` and `FindFFMPEG.cmake` adapt libraries without a usable upstream CMake package config into standard CMake variables/targets. They locate normal system installs; FFmpeg also uses pkg-config when available.

## 🚀 Quick Start

Run the multimedia bridge demo (showcases Skia, FFmpeg, OpenCV, etc., via JS):

```bash
./build/EngineBuilder run demo_app/bridge_multimedia.js
```

## 🔒 Crypto & Compression Support

AudioPerfX includes built-in support for multiple algorithms:
- **Crypto:** OpenSSL `libcrypto` integration (`Crypto` & `Bcrypto`).
- **Compression:** 
  - `Zlib` (standard)
  - `Zstd` (requires `libzstd` dev package)
  - `Brotli` (requires `libbrotli` dev package)

## 📚 Documentation

Detailed documentation and examples can be found in the following locations:
- [Demo App Docs](demo_app/README.md)
- [Examples Docs](example/README.md)
- [JavaScript Modules Docs](src/modules/README.md)
- [Async I/O Docs](src/async_io/README.md)
- [Module Reference](docs/modules.md)
- [Stack Error Handling](docs/stack_error.md)
