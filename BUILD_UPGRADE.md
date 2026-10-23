# XER Build System Upgrade

## Overview

Complete overhaul of the XER build system to remove compatibility layers, use real dependencies, and simplify dependency management.

## Changes Made

### 1. Removed Compatibility Layer

**Deleted:**
- `compat/absl/` directory (11 stub header files)
  - `absl/container/flat_hash_map.h`
  - `absl/hash/hash.h`
  - `absl/strings/ascii.h`
  - `absl/strings/numbers.h`
  - `absl/strings/string_view.h`
  - `absl/strings/strip.h`
  - `absl/strings/str_cat.h`
  - `absl/strings/str_format.h`
  - `absl/strings/str_join.h`
  - `absl/strings/str_replace.h`
  - `absl/strings/str_split.h`

**Reason:**
- Compatibility stubs were incomplete
- Could cause runtime issues
- Added unnecessary complexity
- Real Abseil is better and always available via FetchContent

### 2. Updated Abseil Dependency

**Before:**
```cmake
if(NOT absl_FOUND)
    # Use compat/absl stubs
    set(XER_ABSL_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/compat/absl/include")
    function(xer_add_absl_compat_target target_name)
        # Create interface target with stub headers
    endfunction()
endif()
```

**After:**
```cmake
# Abseil - always fetch via FetchContent for consistency
if(XER_FETCH_DEPENDENCIES)
    message(STATUS "Fetching Abseil via FetchContent")
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    xer_fetch_cmake_dependency(xer_abseil https://github.com/abseil/abseil-cpp.git 20250127.1)
    set(absl_FOUND TRUE)
else()
    find_package(absl QUIET)
    if(NOT absl_FOUND)
        message(FATAL_ERROR "Abseil not found. Please install Abseil or set XER_FETCH_DEPENDENCIES=ON to auto-fetch.")
    endif()
endif()
```

**Benefits:**
- ✅ Real Abseil always used
- ✅ Consistent behavior
- ✅ No runtime issues from stubs
- ✅ Auto-fetch when XER_FETCH_DEPENDENCIES=ON

### 3. Simplified Dependency Management

**Removed:**
- `cmake/deps/auto_build.cmake` (duplicated functionality)

**Updated CMakeLists.txt:**
```cmake
include(cmake/deps/fetch.cmake)
include(cmake/deps/system.cmake)
include(cmake/deps/openssl_external.cmake)
include(cmake/deps/ffmpeg_external.cmake)
include(cmake/deps/libusb_external.cmake)
include(cmake/deps/optional.cmake)
```

**Simplified system.cmake:**
- Removed complex OpenSSL logic (now in openssl_external.cmake)
- Removed complex FFmpeg logic (now in ffmpeg_external.cmake)
- Kept simple libuv auto-fetch
- Kept simple Abseil auto-fetch
- Clear dependency validation

### 4. Updated .gitignore

Added:
```gitignore
# Compatibility layer (removed in favor of real dependencies)
/compat/
```

### 5. V8 Build Optimizations

**Added options:**
- `XER_SKIP_V8_SYNC` - Skip V8 source sync if already present
- Cache checks for depot_tools, V8 source, and V8 builds
- Conditional OpenSSL requirement (only with LLVM/Clang)

### 6. libuv Auto-Fetch

**Added to system.cmake:**
```cmake
if(NOT TARGET libuv::libuv AND XER_FETCH_DEPENDENCIES)
    message(STATUS "libuv not found, fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.48.0
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/libuv-src"
        BINARY_DIR "${XER_DEPS_DIR}/libuv-build"
    )
    FetchContent_MakeAvailable(libuv)
    add_subdirectory("${XER_DEPS_DIR}/libuv-src" "${XER_DEPS_DIR}/libuv-build" EXCLUDE_FROM_ALL)
endif()
```

### 7. libxml2 Auto-Fetch

**Added to system.cmake (required for V8+LLVM/Clang):**
```cmake
if(NOT LibXml2_FOUND AND XER_FETCH_DEPENDENCIES)
    message(STATUS "libxml2 not found, fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        libxml2
        GIT_REPOSITORY https://github.com/GNOME/libxml2.git
        GIT_TAG v2.12.7
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/libxml2-src"
        BINARY_DIR "${XER_DEPS_DIR}/libxml2-build"
    )
    FetchContent_MakeAvailable(libxml2)
    add_subdirectory("${XER_DEPS_DIR}/libxml2-src" "${XER_DEPS_DIR}/libxml2-build" EXCLUDE_FROM_ALL)
endif()
```

### 8. zstd Auto-Fetch

**Added to system.cmake (optional compression):**
```cmake
if(NOT zstd_FOUND AND XER_FETCH_DEPENDENCIES)
    message(STATUS "zstd not found, fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/zstd-src"
        BINARY_DIR "${XER_DEPS_DIR}/zstd-build"
    )
    FetchContent_MakeAvailable(zstd)
    add_subdirectory("${XER_DEPS_DIR}/zstd-src/build/cmake" "${XER_DEPS_DIR}/zstd-build" EXCLUDE_FROM_ALL)
endif()
```

### 9. zlib Auto-Fetch

**Added to system.cmake (required compression):**
```cmake
if(NOT ZLIB_FOUND AND XER_FETCH_DEPENDENCIES)
    message(STATUS "zlib not found, fetching via FetchContent")
    include(FetchContent)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/zlib-src"
        BINARY_DIR "${XER_DEPS_DIR}/zlib-build"
    )
    FetchContent_MakeAvailable(zlib)
    add_subdirectory("${XER_DEPS_DIR}/zlib-src" "${XER_DEPS_DIR}/zlib-build" EXCLUDE_FROM_ALL)
endif()
```

## File Structure After Changes

```
cmake/
├── deps/
│   ├── fetch.cmake              # FetchContent helpers
│   ├── system.cmake            # System dependencies (simplified)
│   ├── optional.cmake          # Optional dependencies
│   ├── v8_external.cmake       # V8 build
│   ├── openssl_external.cmake  # OpenSSL build
│   ├── ffmpeg_external.cmake   # FFmpeg build
│   ├── libusb_external.cmake   # libusb build
│   └── llvm_external.cmake     # LLVM build
├── modules/                    # Find modules
├── sources.cmake               # Source file collection
└── targets/                    # Build targets
```

## Build Options

### Standard Build (All Dependencies)

```bash
cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build
cmake --build build --config Release
```

This will:
- Fetch and build Abseil
- Fetch and build libuv
- Build OpenSSL (if ENABLE_V8=ON and LLVM/Clang available)
- Build FFmpeg (if ENABLE_MEDIA=ON)
- Build V8 (if ENABLE_V8=ON)

### Cached V8 Build

```bash
cmake -DXER_FETCH_DEPENDENCIES=ON -DXER_SKIP_V8_SYNC=ON -S . -B build
cmake --build build --config Release
```

### Build Without V8

```bash
cmake -DXER_FETCH_DEPENDENCIES=ON -DENABLE_V8=OFF -S . -B build
cmake --build build --config Release
```

### System Dependencies Only

```bash
cmake -DXER_FETCH_DEPENDENCIES=OFF -S . -B build
cmake --build build --config Release
```

Requires:
- Abseil installed
- libuv installed
- OpenSSL installed (if V8 enabled)
- FFmpeg installed (if media enabled)

## Dependencies

### Required (Always)
- **Abseil**: Auto-fetched or system
- **zlib**: Auto-fetched or system

### Auto-Fetched (When XER_FETCH_DEPENDENCIES=ON)
- **libuv**: Required for V8 and network
- **libxml2**: Required for V8+LLVM/Clang
- **zstd**: Optional compression library
- **zlib**: Required compression library

### Optional (By Feature)
- **OpenSSL**: Required for V8+LLVM/Clang and network
- **FFmpeg**: Required for media features
- **LLVM/Clang**: Required for XER executable
- **V8**: Required for JavaScript runtime

## Verification

After the upgrade, verify the build:

```bash
# Clean build
rm -rf build .deps

# Configure
cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build

# Build
cmake --build build --config Release

# Verify compat/ is gone
ls compat/  # Should fail: No such file or directory

# Verify real Abseil is used
grep -r "compat/absl" build/  # Should find nothing
```

## Benefits

### Build System
- ✅ Simpler dependency management
- ✅ Clear separation of concerns
- ✅ No duplicate functionality
- ✅ Better error messages
- ✅ Consistent behavior

### Code Quality
- ✅ Real dependencies instead of stubs
- ✅ No runtime issues from incomplete implementations
- ✅ Up-to-date library versions
- ✅ Proper testing support

### Maintainability
- ✅ Easier to understand
- ✅ Easier to modify
- ✅ Fewer files to maintain
- ✅ Clear upgrade path

## Migration Guide

### For Developers

**Before:**
```bash
cmake -S . -B build
cmake --build build
```

**After:**
```bash
cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build
cmake --build build
```

### For CI/CD

**Before:**
```yaml
- cmake -S . -B build
- cmake --build build
```

**After:**
```yaml
- cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build
- cmake --build build
```

### For Package Managers

**Before:**
Could rely on compat/absl stubs

**After:**
Must install real Abseil or use XER_FETCH_DEPENDENCIES=ON

## Troubleshooting

### Error: Abseil not found

**Solution:**
```bash
cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build
```

Or install Abseil manually:
```bash
# Linux
sudo apt install libabsl-dev

# macOS
brew install abseil

# Windows (vcpkg)
vcpkg install abseil-cpp:x64-windows
```

### Error: compat/absl/include not found

**Solution:**
This should not happen anymore. Ensure you're using the updated CMake files:
```bash
git pull
rm -rf build .deps
cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build
```

### Error: libuv not found

**Solution:**
```bash
cmake -DXER_FETCH_DEPENDENCIES=ON -S . -B build
```

Or install libuv manually:
```bash
# Linux
sudo apt install libuv1-dev

# macOS
brew install libuv

# Windows (vcpkg)
vcpkg install libuv:x64-windows
```

## Rollback

If you need to rollback these changes:

```bash
git checkout HEAD -- cmake/ CMakeLists.txt .gitignore
```

Note: The compat/ directory was deleted and won't be restored by git checkout.

## Summary

The build system upgrade provides:
- **Simplicity**: Removed complexity and duplication
- **Reliability**: Real dependencies instead of stubs
- **Consistency**: Uniform behavior across all builds
- **Maintainability**: Easier to understand and modify
- **Performance**: Better caching and incremental builds
