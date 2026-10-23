# XER 🚀

XER is a high-performance C++/V8 runtime builder that executes JavaScript project configurations and compiles native code dynamically through `RuntimeLive`. It provides a powerful bridge between JavaScript and advanced C++ multimedia/GPU frameworks.

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Architecture](#architecture)
- [Build Instructions](#build-instructions)
- [Quick Start](#quick-start)
- [Audio Subsystem](#audio-subsystem)
- [UI Module](#ui-module)
- [Lunca Audio Streaming](#lunca-audio-streaming)
- [Network Module](#network-module)
- [Project Layout](#project-layout)
- [Documentation](#documentation)
- [Development](#development)
- [License](#license)

## Overview

XER is designed for high-performance multimedia and GPU-accelerated applications. It combines:
- A JavaScript-driven build system via `EngineBuilder`
- Dynamic native compilation with Clang/LLVM integration
- Rich multimedia bridges (Skia, FFmpeg, OpenCV, CUDA, cuDNN, ANGLE)
- Comprehensive async I/O and hardware abstraction layers
- Advanced audio processing with SIMD acceleration

## Key Features

### JavaScript-Driven Build Flows
- Orchestrate builds easily via `EngineBuilder`
- Support for FlowScript and XerScript
- Dynamic runtime compilation

### Dynamic Native Compilation
- Real-time C/C++ compilation with Clang/LLVM
- In-process compile path via `RuntimeLive`
- Runtime code generation and execution

### Multimedia & GPU Bridges
- **Skia**: 2D graphics rendering
- **FFmpeg**: Audio/video processing
- **OpenCV**: Computer vision
- **CUDA/cuDNN**: GPU computing and deep learning
- **ANGLE**: OpenGL ES to Vulkan/DirectX translation

### Rich Module System
- Filesystem operations
- Cryptography (OpenSSL integration)
- Compression (Zlib, Zstd, Brotli)
- Networking utilities
- Asynchronous I/O helpers

### Async I/O System
- High-performance async file operations
- Thread pool management
- Hardware abstraction (CPU, GPU, memory monitoring)
- DNN backends (cuDNN integration)

## Architecture

### Core Components

```
src/
├── main.cc                 # Application entry point
├── app_command.*           # CLI command parsing
├── compilerapi/            # LLVM/Clang integration
├── v8/                     # V8 JavaScript engine binding
├── javascript/             # JavaScript runtime
├── flowscript/             # FlowScript interpreter
├── runtime_safety/        # Runtime safety and integrity checks
├── async_io/               # Async I/O system
│   ├── hardware_abstraction/  # CPU/GPU monitoring
│   ├── dnn_backends/          # cuDNN integration
│   └── sync_primitives/       # Thread synchronization
├── audio/                  # Audio processing subsystem
│   ├── audio_core/           # Core audio engine
│   ├── dsp_algorithms/       # DSP algorithms
│   ├── effects_rack/         # Audio effects
│   └── analysis_ai/          # Audio analysis
├── modules/                # JavaScript modules
└── xer/                    # XER encoding/compilation
```

## Build Instructions

### Prerequisites

Install CMake (3.21+), Git, and a supported C/C++ toolchain. With `XER_FETCH_DEPENDENCIES=ON` (the default), XER CMake downloads dependencies into the project-local `.deps` directory and reuses them on subsequent configure runs.

### Environment Variables

- `XER_FETCH_DEPENDENCIES`: Enable/disable automatic dependency fetching (default: ON)
- `XER_DEPS_DIR`: Directory for dependency cache (default: `.deps`)
- `ENABLE_CUDA`: Enable CUDA support (default: OFF)
- `ENABLE_CUDNN`: Enable cuDNN support (default: OFF)
- `ENABLE_AI`: Enable AI and model-builder sources (default: OFF)
- `ENABLE_OPENCV`: Fetch and build OpenCV (default: OFF)
- `ENABLE_V8`: Build V8 and JavaScript runtime (default: OFF)
- `ENABLE_MEDIA`: Enable FFmpeg and audio/media dependencies (default: ON)

### Compilation

```bash
# Configure with the platform's default generator
cmake --preset default

# Build
cmake --build --preset default

# Build V8 and the JavaScript runtime without media dependencies
cmake --preset v8
cmake --build --preset v8

# Reconfigure after LLVM/Clang installation, then build XER
cmake --preset v8
cmake --build --preset v8-xer

# Build the OpenCV bridge and its libraries
cmake --preset opencv
cmake --build --preset opencv
```

### Dependency Details

The `default` preset enables media support and fetches:
- Core: Abseil, pthreadpool, FXdiv, zlib, SQLite
- Media: zstd, Brotli, FFmpeg, libusb

With `ENABLE_V8=ON`, XER also fetches:
- V8 (via depot_tools)
- LLVM/Clang
- libuv, LibXml2, libcurl, OpenSSL

OpenCV's optional ADE, IPP IW, ITT, TBB, and bundled FFmpeg downloads are disabled.

## Quick Start

### Run Multimedia Bridge Demo

```bash
./build/EngineBuilder run demo_app/bridge_multimedia.js
```

This demo showcases:
- Skia graphics rendering
- FFmpeg audio/video processing
- OpenCV computer vision
- CUDA/cuDNN operations (if enabled)

### Run Module Smoke Tests

```bash
./out/build/default/EngineBuilder run demo_app/modules_smoke.js
```

### Audio Processing Examples

```bash
# Audio effects
./build/EngineBuilder audiofxcustom --input input.mp3 --effect reverb

# Audio analysis
./build/EngineBuilder audioinspect --input input.mp3

# Spectrogram generation
./build/EngineBuilder spectrogram --input input.mp3
```

## Audio Subsystem

### DSP/Effects Features

- **SIMD/Parallelism**: All major DSP/effects use AVX2/SSE2 and multithreading
- **Zero-Copy**: Zero-copy support for ProcessBlock/ProcessSample operations
- **Async API**: Async processing via std::future (e.g., ProcessBlockAsync)
- **Memory Stats**: GetMemoryStats() for memory reporting
- **Comprehensive Test Suite**: See tests/dsp_memory_async_zero_copy_test.cc

### Audio Loader Features

- **Strict MP3 Support**: Primary MP3 format with fallback for others
- **Error Handling**: AudioLoaderError enum and error category
- **Async API**: LoadAsync and std::future for non-blocking operations
- **SIMD/Multithreading**: SSE2/AVX2 acceleration with thread pool
- **Profiling**: ScopedTimer for decode/process timing
- **Self-Test**: SelfTest() method for validation

### Audio Usage

```cpp
#include "audio_core/audio_source_loader.h"

AudioSourceLoadOptions opts;
opts.input_path = "file.mp3";
AudioSourceBuffer buf;
std::string err;
auto code = AudioSourceLoader::LoadWithCode(opts, &buf, &err);
if (code != AudioLoaderError::OK) {
    // Handle error
}
```

### Audio Self-Test

```cpp
std::string report;
bool ok = AudioSourceLoader::SelfTest(&report);
std::cout << report;
```

## UI Module

XER includes a modern WinAPI-based UI framework with deep module architecture.

### UI Features

- **Deep Module Design**: Small interface with maximum hidden complexity
- **ColorSpace Support**: Full support for sRGB, Display P3, Rec.2020, and HDR (PQ/HLG)
- **HDR Rendering**: Windows HDR API integration for high dynamic range displays
- **ShadcnUI Style**: Modern, clean widget design inspired by ShadcnUI
- **Pure WinAPI**: No external UI dependencies, direct Windows API integration

### Core UI Components

- **Application**: Main entry point with lifecycle management
- **Window**: Window management with widget tree support
- **ColorSpace**: Color space enumeration and utilities
- **HDRManager**: HDR display detection and configuration
- **ColorManager**: Unified color management with conversion
- **Widget System**: Base widget interface for custom components

### UI Usage

```cpp
#include "ui/core/application.h"
#include "ui/core/window.h"
#include "ui/colorspace/color_manager.h"

using namespace XER::UI;
using namespace XER::UI::ColorSpace;

int main() {
    // Initialize application
    auto& app = Application::Instance();
    app.Initialize();

    // Setup ColorSpace/HDR
    auto& color_mgr = ColorManager::Instance();
    color_mgr.Initialize();

    if (color_mgr.IsHDRCapable()) {
        color_mgr.SetPreferredColorSpace(ColorSpace::Rec2100_PQ);
    }

    // Create window
    Window window;
    window.Create("XER UI Demo", 800, 600);

    // Enable HDR if available
    if (color_mgr.IsHDRCapable()) {
        color_mgr.GetHDRManager().EnableHDR(window.GetHWND());
    }

    window.Show();
    return app.Run();
}
```

### Supported Color Spaces

- **sRGB**: Standard sRGB (no HDR)
- **DisplayP3**: Wide gamut (Apple displays)
- **Rec2020**: Ultra-wide gamut (high-end displays)
- **Rec2100_PQ**: HDR with Perceptual Quantizer
- **Rec2100_HLG**: HDR with Hybrid Log-Gamma
- **ScRGB**: HDR with linear transfer

### Documentation

- [UI Module README](src/ui/README.md) - Comprehensive UI documentation
- [UI Architecture](src/ui/ARCHITECTURE.md) - Detailed architecture and design

## Lunca Audio Streaming

XER includes a high-performance audio streaming system for real-time audio transmission.

### Lunca Features

- **Low Latency**: Optimized for minimal delay audio streaming
- **UDP-Based**: Uses UDP for maximum speed and efficiency
- **Custom URI Protocol**: Supports `lunca://<IP>/on/<STREAM_ID>` for stream control
- **Deep Module Architecture**: Small interfaces with maximum hidden complexity
- **XER Integration**: Uses XER audio subsystem for device abstraction
- **Opus Codec**: High-efficiency audio compression
- **Cross-Platform**: Windows, Linux, macOS support

### Lunca Usage

```cpp
#include "lunca/common/udp_socket.h"
#include "lunca/common/packet.h"

using namespace Lunca::Common;

// Create UDP socket
auto socket = UDPSocket::Create();
socket->Bind("0.0.0.0", 12345);

// Create audio packet
AudioPacketHeader header(1, 100, 5000);
AudioPacket packet(header);
packet.SetPayload(encoded_audio);

// Send packet
socket->SendTo(packet.Serialize(), "192.168.1.100", 12345);
```

### Lunca Documentation

- [Lunca README](src/lunca/README.md) - Comprehensive Lunca documentation
- [Lunca Architecture](src/lunca/ARCHITECTURE.md) - Detailed architecture and design

## Network Module

XER includes a comprehensive network stack inspired by Chromium's architecture.

### Network Features

- **Full Protocol Support**: HTTP/1.1, HTTP/2, HTTP/3 (QUIC), WebSocket, FTP, SPDY
- **URL Handling**: Complete URL parsing, validation, and normalization
- **Request/Response Management**: Create, send, and process network requests
- **HTTP Caching**: Integrated HTTP cache for accelerated resource loading
- **SSL/TLS Security**: Certificate verification and modern TLS support
- **DNS Resolution**: Efficient DNS resolution with caching
- **Proxy Support**: HTTP, HTTPS, SOCKS4/5 proxy support
- **Low-Level Sockets**: TCP, UDP, SSL/TLS socket operations
- **Asynchronous Model**: Event-driven architecture with thread pool integration

### Network Usage

```cpp
#include "content/network/url/url_request_util.h"

// High-level: synchronous GET
std::string response = network::url::SyncGet("http://example.com/api");

// WebSocket connection
#include "content/network/websocket/websocket_stream.h"

network::websocket::WebSocketStream ws;
std::string error;
if (!ws.DoHandshake("ws://echo.websocket.org", {}, &error)) {
    // handle error
}
ws.SendText("Hello", [](bool ok) { /* callback */ });
```

### Network Documentation

- [Network README](src/content/network/README.md) - Comprehensive network documentation
- [Network Architecture](src/content/network/ARCHITECTURE.md) - Detailed architecture
- [Network Improvements](src/content/network/IMPROVEMENTS.md) - Improvement roadmap

## Crypto & Compression Support

XER includes built-in support for multiple algorithms:

### Crypto
- OpenSSL `libcrypto` integration
- `Crypto` & `Bcrypto` modules

### Compression
- **Zlib**: Standard compression
- **Zstd**: High-speed compression (requires libzstd)
- **Brotli**: Modern compression (requires libbrotli)

## Project Layout

```
XER/
├── src/                   # Core runtime engine
│   ├── async_io/         # Async I/O primitives
│   ├── audio/            # Audio processing subsystem
│   ├── ui/               # WinAPI UI framework
│   │   ├── core/         # Core framework (Application, Window)
│   │   ├── colorspace/   # ColorSpace & HDR support
│   │   ├── widgets/      # Widget system
│   │   ├── layout/       # Layout system
│   │   └── theme/        # ShadcnUI-style theming
│   ├── lunca/            # Audio streaming system
│   │   ├── common/       # Shared utilities (UDP, packets)
│   │   ├── streaming/    # Audio streaming logic
│   │   ├── protocol/     # Protocol handling
│   │   ├── server/       # Server implementation
│   │   └── client/       # Client implementation
│   ├── content/          # Content subsystems
│   │   └── network/      # Network stack (HTTP, WebSocket, QUIC, etc.)
│   │       ├── core/     # Network context and service
│   │       ├── url/      # URL handling
│   │       ├── http/     # HTTP protocol
│   │       ├── websocket/# WebSocket protocol
│   │       ├── quic/     # QUIC protocol
│   │       ├── socket/   # Socket abstractions
│   │       ├── dns/      # DNS resolution
│   │       ├── proxy/    # Proxy support
│   │       ├── cert/     # Certificate management
│   │       └── ...       # Other network components
│   ├── modules/          # JavaScript modules
│   └── v8/               # V8 engine integration
├── demo_app/             # Demo configurations and scripts
├── cmake/                # CMake configuration
│   ├── deps/             # Dependency management
│   ├── modules/          # CMake find modules
│   └── targets/          # Build targets
├── tests/                # Unit and integration tests
├── plugins/              # Plugin system
├── compat/               # Compatibility shims
└── docs/                 # Documentation
```

## Documentation

- [Demo App Docs](demo_app/README.md)
- [JavaScript Modules Docs](src/modules/README.md)
- [Async I/O Docs](src/async_io/README.md)
- [Audio Subsystem Docs](src/audio/README.md)
- [UI Module Docs](src/ui/README.md)
- [UI Architecture](src/ui/ARCHITECTURE.md)
- [Lunca Audio Streaming Docs](src/lunca/README.md)
- [Lunca Architecture](src/lunca/ARCHITECTURE.md)
- [Network Module Docs](src/content/network/README.md)
- [Network Architecture](src/content/network/ARCHITECTURE.md)
- [Network Improvements](src/content/network/IMPROVEMENTS.md)
- [Module Reference](docs/modules.md)
- [Stack Error Handling](docs/stack_error.md)

## Development

### Running Tests

```bash
# DSP/Effects tests
cd tests
g++ dsp_memory_async_zero_copy_test.cc -I../src -std=c++17 -lpthread -o dsp_test
./dsp_test

# Module smoke tests
./build/EngineBuilder run demo_app/modules_smoke.js
```

### Code Style

- C++23 standard
- Follow existing code conventions
- Use consistent naming patterns
- Document public APIs

### Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Add tests for new functionality
4. Ensure all tests pass
5. Submit a pull request

## TODO

### Audio Subsystem
- Implement full mpg123 fallback for MP3 decode
- Add more unit tests (valid MP3, WAV, error cases)
- Integrate async decode in effect chains
- Advanced error propagation and profiling

### General
- Improve test coverage across all modules
- Enhance documentation and examples
- Optimize build times and dependency management
- Add performance benchmarks

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

# Reconfigure after LLVM/Clang installation, then build XER
cmake --preset v8
cmake --build --preset v8-xer

# Build the OpenCV bridge and its libraries
cmake --preset opencv
cmake --build --preset opencv
```

The `default` preset enables media support. Core builds fetch Abseil, pthreadpool, FXdiv, zlib, and SQLite; SQLite is built from the pinned amalgamation under `.deps`. Media support also fetches zstd, Brotli, FFmpeg, and libusb. Enable `ENABLE_OPENCV=ON` to have XER CMake fetch and build OpenCV; `ENABLE_AI=ON` also enables that dependency. OpenCV's optional ADE, IPP IW, ITT, TBB, and bundled FFmpeg downloads are disabled, and its download cache is kept under `.deps`.

With `ENABLE_V8=ON`, XER fetches depot_tools and builds a pinned V8 monolith, as well as fetching libuv, LibXml2, libcurl, and OpenSSL. The `v8` build preset also builds LLVM/Clang from pinned sources into `.deps/llvm-install`, along with `XERJavaScript` and SQLite. After that first build, configure the `v8` preset again so CMake discovers LLVM/Clang, then build the `v8-xer` preset to build the XER executable. These source builds require Python, Git, Ninja through depot_tools, and the platform's native compiler toolchain. LLVM/Clang requires substantial disk space and build time. OpenSSL requires Perl plus `make`/`nmake` to build; when those tools are unavailable, an installed OpenSSL package is required for XER. On Windows, FFmpeg builds from source with MSVC when Bash and GNU Make (for example from MSYS2) are available; otherwise CMake downloads the source and uses an installed FFmpeg package if present. CUDA Toolkit/cuDNN are also external SDK prerequisites.

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
