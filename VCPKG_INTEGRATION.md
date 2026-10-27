# Vcpkg Integration Guide

## Overview

XER is configured to use vcpkg for dependency management without requiring a `vcpkg.json` manifest file. Instead, it uses the already installed libraries from the vcpkg triplet `x64-windows-release`.

## Configuration

### vcpkg Path

The project is configured to use vcpkg from `C:/vcpkg`. This is set in two places:

1. **CMakeLists.txt** - Sets the toolchain file path
2. **CMakePresets.json** - Configures the toolchain for each preset

### Triplet

The project uses the `x64-windows-release` triplet for optimized release builds.

### Dependency Fetching

By default, automatic dependency fetching is **disabled**. Dependencies should be installed via vcpkg manually:

```bash
vcpkg install libuv:x64-windows-release
```

For V8, `XER_FETCH_V8=ON` exposes the explicit `xer_v8_fetch` target. Neither CMake configuration nor `xer_v8_build` downloads sources. Run the fetch target before configuring or building GN. This is independent of `XER_FETCH_DEPENDENCIES`.

## Using vcpkg Libraries

### Adding a New vcpkg Dependency

To use a library installed in vcpkg, add the following to your CMake file:

```cmake
find_package(<package_name> CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE <package_name>::<target_name>)
```

Example for OpenSSL:
```cmake
find_package(OpenSSL REQUIRED)
target_link_libraries(my_target PRIVATE OpenSSL::SSL OpenSSL::Crypto)
```

### Available vcpkg Libraries

Common libraries that may be available in your vcpkg installation:

- **libuv** - Asynchronous I/O
- **OpenSSL** - SSL/TLS
- **zlib** - Compression
- **zstd** - Zstandard compression
- **brotli** - Brotli compression
- **sqlite3** - SQLite database
- **curl** - HTTP client
- **libxml2** - XML parsing
- **ffmpeg** - Audio/video processing
- **opencv** - Computer vision
- **abseil** - Google Abseil libraries (`find_package(absl CONFIG REQUIRED)`)
- **fmt** - Formatting library
- **jsoncpp** - JSON parsing and generation
- **re2** - Regular expressions
- **pthreadpool** - Thread pool
- **fxdiv** - Fast integer division
- **SDL2** - Audio/graphics

### Currently Used vcpkg Libraries in XER

The following vcpkg libraries are integrated into XER:

- **libuv** - Async I/O operations
- **absl** (strings, synchronization, flat_hash_map, flat_hash_set) - Core utilities
- **fmt** - String formatting
- **jsoncpp** - JSON handling
- **re2** - Regular expressions
- **sqlite3** - Database operations
- **zlib** - Compression
- **zstd** - Zstandard compression
- **brotli** - Brotli compression
- **OpenSSL** - SSL/TLS
- **curl** - HTTP client
- **FFmpeg** - Audio/video processing (optional)
- **SDL2** - Audio device access (optional)
- **OpenCV** - Computer vision (optional, requires ENABLE_OPENCV)
- **pthreadpool** - Parallel processing
- **fxdiv** - Fast division
- **fftw3** - Wavetable audio support (application builds)
- **ICU** - V8 internationalization

To check what's installed:
```bash
vcpkg list
```

## Build Presets

The project includes several CMake presets configured for vcpkg:

### Default
```bash
cmake --preset default
cmake --build --preset default
```
- Release build
- Media dependencies enabled
- Uses vcpkg toolchain
- No automatic dependency fetching

### V8
```bash
cmake --preset v8
cmake --build --preset v8-fetch
cmake --build --preset v8
ctest --preset v8
```
- Release build
- V8 and a JavaScript/Intl smoke test enabled (`XER_BUILD_APP=OFF`)
- V8 fetched by the separate `v8-fetch` build preset
- Media dependencies disabled
- No automatic dependency fetching

### V8-XER
```bash
cmake --preset v8-xer
cmake --build --preset v8-xer --target xer_v8_fetch
cmake --build --preset v8-xer
```
- Release build
- V8 and JavaScript runtime enabled
- V8 fetched by the explicit `xer_v8_fetch` target
- Media dependencies enabled
- Full XER executable
- No automatic dependency fetching

### OpenCV
```bash
cmake --preset opencv
cmake --build --preset opencv
```
- Release build
- OpenCV bridge enabled
- No automatic dependency fetching

### Debug
```bash
cmake --preset debug
cmake --build --preset debug
```
- RelWithDebInfo build with the release DLL CRT (`/MD`)
- Uses vcpkg toolchain
- No automatic dependency fetching

## Manual Configuration

If you prefer not to use presets, you can configure manually:

```bash
cmake -B build -S . ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
  -DXER_FETCH_DEPENDENCIES=OFF ^
  -DENABLE_MEDIA=ON
```

With V8 (exposes the separate fetch target):
```bash
cmake -B build -S . ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-release ^
  -DXER_FETCH_DEPENDENCIES=OFF ^
  -DXER_FETCH_V8=ON ^
  -DENABLE_V8=ON ^
  -DENABLE_MEDIA=OFF
cmake --build build --target xer_v8_fetch --config Release
cmake --build build --target xer_v8_smoke --config Release
```

## V8 Source Build

The integration pins V8 `13.2.152.28` to commit `3d13fb751e6f2ecb9aeac06e65367704a12f5c19` and depot_tools to `159aabc8235aeccb463493c999c182b6485a1381`. The V8 version is retained from the project's previous build configuration. Changing versions requires checking the GN adapters and the Windows CRT patch against the new sources.

Use an MSVC/Windows SDK installation supported by this V8 revision (its Chromium build scripts recognize Visual Studio 2022). Availability of a newer Visual Studio for CMake does not establish compatibility with V8's older GN scripts.

Install `icu:x64-windows-release` and `zlib:x64-windows-release` before configuring V8. The generated `args.gn` includes absolute include and import-library paths discovered through vcpkg. `v8_icu_path` and `v8_zlib_path` select project-supplied GN targets; they are not filesystem library search paths.

The monolithic library uses `is_debug=false`, `is_component_build=false`, `v8_use_external_startup_data=false`, the MSVC C++ standard library and the dynamic release CRT. The preparation step makes a checked, repeatable change to `build/config/win/BUILD.gn` in the downloaded checkout, enabling `/MD` for both normal targets and release-CRT tools. Unsupported upstream layouts fail rather than silently producing an incompatible library. Internal dependencies other than ICU and zlib remain managed by V8's pinned `DEPS`.

Configuration generates `out/build/<preset>/v8-config/args.gn`. `xer_v8_configure` copies it into the GN output directory and runs `gn gen --fail-on-unused-args`. `xer_v8_build` does the same and builds `v8_monolith`. `V8::V8` supplies `V8_GN_HEADER` and the generated header directory so consumers use the same public feature definitions.

Available CMake cache settings:

- `XER_V8_DEPS_DIR`: workspace for V8 and depot_tools (default `.deps/v8`).
- `XER_V8_SOURCE_DIR`: source checkout (default `${XER_V8_DEPS_DIR}/v8`).
- `XER_DEPOT_TOOLS_DIR`: pinned depot_tools checkout.
- `XER_V8_BUILD_DIR`: GN output directory (default `${XER_V8_SOURCE_DIR}/out/xer-release`).
- `XER_V8_NINJA_EXECUTABLE`: path to Ninja, discovered from `PATH`.
- `XER_V8_BUILD_JOBS`: positive integer, default `4`.
- `XER_BUILD_APP`: build application subsystems, default `ON`.

Existing source checkouts must match the pinned revision. The fetch target refuses to reset other revisions or replace a different `.gclient`. Configuration does not require sources to exist; the build reports the missing fetch step explicitly.

vcpkg's app-local deployment copies dependency DLLs beside the CMake-built executables. The V8 driver also adds the selected triplet's `bin` directory to the environment of GN/Ninja so snapshot and internationalization tools can load ICU and zlib.

## Dependency Verification

From an x64 Native Tools command prompt with Ninja on `PATH`:

```bash
cmake --preset default
cmake --build --preset default --target xer_dependencies_smoke
ctest --preset default
```

`xer_dependencies_smoke` links and calls the core libraries, SQLite, compression libraries, OpenSSL/curl, pthreadpool, FFmpeg and SDL2. `v8_prepare` exercises CRT selection, paths containing spaces, repeated preparation and rejection of unsupported upstream layouts. `ctest --preset v8` runs the JavaScript/Intl test after the separate V8 build.

## Troubleshooting

### Library Not Found

If CMake cannot find a vcpkg library:

1. Verify the library is installed:
   ```bash
   vcpkg list
   ```

2. Install the library if needed:
   ```bash
   vcpkg install <package_name>:x64-windows-release
   ```

3. Check the triplet is correct in your build configuration

### Wrong Triplet

If you see errors about mismatched architectures:

- Ensure `VCPKG_TARGET_TRIPLET` is set to `x64-windows-release`
- Reconfigure CMake after changing the triplet

### Path Issues

If vcpkg is not at `C:/vcpkg`:

- Set the `VCPKG_ROOT` environment variable
- Or update the `CMAKE_TOOLCHAIN_FILE` path in CMakeLists.txt or CMakePresets.json

## Integration Without vcpkg.json

This project does not use a `vcpkg.json` manifest file. Instead:

1. Dependencies are managed through CMake's `find_package`
2. Libraries are assumed to be pre-installed in vcpkg
3. The toolchain file handles integration automatically

This approach gives you:
- More control over which libraries to use
- Ability to mix vcpkg and non-vcpkg dependencies
- Simpler CMake configuration
- Compatibility with existing build systems

## Project Structure

### CMake Files

- **CMakeLists.txt** (root) - Main CMake configuration with vcpkg integration
- **CMakePresets.json** - Build presets for different configurations
- **src/CMakeLists.txt** - Main executable configuration
- **src/async_io/CMakeLists.txt** - Async I/O library
- **src/audio/CMakeLists.txt** - Audio subsystem
- **src/audio/dsp_algorithms/CMakeLists.txt** - DSP algorithms
- **src/content/CMakeLists.txt** - Content subsystem
- **src/content/network/CMakeLists.txt** - Network stack
- **src/content/openai/CMakeLists.txt** - OpenAI integration
- **src/ui/CMakeLists.txt** - UI framework
- **src/lunca/CMakeLists.txt** - Audio streaming

### Library Targets

- **xer** - Main executable
- **XER_AsyncIO** - Async I/O primitives
- **XER_AudioCore** - Core audio engine
- **XER_AudioDSP** - DSP algorithms
- **XER_Content** - Content subsystem
- **XER_Network** - Network stack
- **XER_OpenAI** - OpenAI integration
- **XER_UI_Core** - UI framework
- **XER_Lunca** - Audio streaming system

## Optimization Flags

The project uses optimized compiler flags:

### MSVC (Windows)
- `/O2` - Optimize for speed
- `/arch:AVX2` - Enable AVX2 instructions (audio/dsp)
- `/W4` - High warning level
- `/WX-` - Treat warnings as errors (disabled for compatibility)

### GCC/Clang (Linux/macOS)
- `-O3` - Maximum optimization
- `-march=native` - Optimize for current CPU
- `-msse2` - Enable SSE2 instructions
- `-mavx2` - Enable AVX2 instructions (audio/dsp)
- `-Wall -Wextra -Wpedantic` - High warning level

## Environment Variables

Optional environment variables:

- `VCPKG_ROOT` - Path to vcpkg installation (auto-detected if not set)
- `VCPKG_DEFAULT_TRIPLET` - Default triplet (project overrides to x64-windows-release)

## Next Steps

1. Ensure vcpkg is installed at `C:/vcpkg`
2. Install required libraries manually:
   ```bash
   vcpkg install libuv:x64-windows-release
   vcpkg install abseil:x64-windows-release
   vcpkg install fmt:x64-windows-release
   vcpkg install jsoncpp:x64-windows-release
   vcpkg install re2:x64-windows-release
   vcpkg install sqlite3:x64-windows-release
   vcpkg install zlib:x64-windows-release
   vcpkg install zstd:x64-windows-release
   vcpkg install brotli:x64-windows-release
   vcpkg install pthreadpool:x64-windows-release
   vcpkg install fxdiv:x64-windows-release
   vcpkg install fftw3:x64-windows-release
   ```
3. For media support (optional):
   ```bash
   vcpkg install ffmpeg:x64-windows-release
   vcpkg install sdl2:x64-windows-release
   ```
4. For networking (optional):
   ```bash
   vcpkg install openssl:x64-windows-release
   vcpkg install curl:x64-windows-release
   ```
5. For V8 support, enable the fetch option:
   ```bash
   cmake --preset v8
   cmake --build --preset v8-fetch
   cmake --build --preset v8
   ```
   The separate fetch step obtains V8 and its upstream build dependencies.
6. For regular builds without V8:
   ```bash
   cmake --preset default
   cmake --build --preset default
   ```
