# XER Build Configuration Fixes

## Overview

This document describes comprehensive build system improvements including:
- V8 build optimization
- Dependency management overhaul
- Removal of compatibility layer (compat/absl)
- Use of real Abseil library
- Proper external dependency integration

## Problem 1: XERV8SourceSync Build Error

The build was failing with error MSB8066 for XERV8SourceSync custom build rule. This error occurs when ExternalProject custom build commands fail during the build process.

### Root Cause

The V8 external project (XERV8SourceSync) was configured to always run gclient sync commands, even when:
1. Depot tools were already downloaded
2. V8 source was already present
3. Build artifacts already existed

This caused unnecessary rebuild attempts and potential failures.

### Fixes Applied

#### 1. Added Build Cache Option (CMakeLists.txt)

Added a new CMake option to skip V8 source sync when already present:

```cmake
option(XER_SKIP_V8_SYNC "Skip V8 source sync if already present (useful for cached V8 builds)" OFF)
```

**Usage:**
```bash
cmake -DXER_SKIP_V8_SYNC=ON ..
```

#### 2. Depot Tools Download Optimization (v8_external.cmake)

Added check to skip depot_tools download if already present.

#### 3. V8 Source Sync Optimization (v8_external.cmake)

Added intelligent V8 source sync with three scenarios:
- Scenario 1: V8 source exists and XER_SKIP_V8_SYNC=ON → Skip sync
- Scenario 2: V8 source missing but XER_SKIP_V8_SYNC=ON → Warn and proceed
- Scenario 3: Default (XER_SKIP_V8_SYNC=OFF) → Always run sync

#### 4. V8 Build Optimization (v8_external.cmake)

Added check to skip V8 build if library already exists.

#### 5. JavaScript Library Validation (javascript.cmake)

Added validation to ensure V8 is properly built before creating XERJavaScript library.

## Problem 2: OpenSSL Dependency Error

**Error:**
```
CMake Error at cmake/deps/system.cmake:71 (message):
  ENABLE_V8=ON requires OpenSSL
```

### Root Cause

The CMake configuration required OpenSSL for V8 builds, even when LLVM/Clang was not available. OpenSSL is only actually needed when LLVM/Clang is present (for the full XER executable), not for building V8 alone.

### Fix Applied

Modified `cmake/deps/system.cmake` to:

1. **Conditional OpenSSL requirement**: OpenSSL is now only required when BOTH `ENABLE_V8=ON` AND `ENGINE_HAS_LLVM_CLANG=1` (LLVM/Clang is available)

2. **Graceful degradation**: When V8 is enabled but LLVM/Clang is not available:
   - V8 runtime can still be built
   - XER executable will be disabled
   - OpenSSL is optional (warning if missing)

3. **Clear error messages**: Improved error messages with installation instructions:
   - Windows: `vcpkg install openssl`
   - Linux: `apt install libssl-dev`

### Updated Logic

```cmake
# Only require OpenSSL when LLVM/Clang is available
if(ENABLE_V8 AND ENGINE_HAS_LLVM_CLANG)
    if(NOT OpenSSL_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON with LLVM/Clang requires OpenSSL. "
            "Install OpenSSL manually (Windows: vcpkg install openssl, Linux: apt install libssl-dev)")
    endif()
elseif(ENABLE_V8 AND NOT ENGINE_HAS_LLVM_CLANG)
    # V8 can be built without LLVM/Clang
    message(STATUS "LLVM/Clang not found: V8 runtime will be built, but XER executable will be disabled")
    if(NOT OpenSSL_FOUND)
        message(WARNING "OpenSSL not found: some V8 features may be limited")
    endif()
endif()
```

## Build Configuration Options

### Standard Build (V8 Enabled, No LLVM/Clang)

```bash
# Configure (OpenSSL not required)
cmake -DENABLE_V8=ON -S . -B build

# Build
cmake --build build --config Release
```

This will:
- Build V8 monolithic library
- Build XER without LLVM/Clang features
- OpenSSL is optional (warning if missing)

### Full Build (V8 + LLVM/Clang)

```bash
# Configure (OpenSSL required)
cmake -DENABLE_V8=ON -S . -B build

# Build
cmake --build build --config Release
```

This will:
- Build V8 monolithic library
- Build XER with LLVM/Clang features
- OpenSSL is required (error if missing)

### Build Without V8

```bash
# Configure
cmake -DENABLE_V8=OFF -S . -B build

# Build
cmake --build build --config Release
```

This will:
- Skip all V8-related steps
- Build XER without JavaScript runtime
- OpenSSL not required

## Installing OpenSSL

### Windows

**Option 1: vcpkg (Recommended)**
```bash
# Install vcpkg if not already installed
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Install OpenSSL
.\vcpkg install openssl:x64-windows

# Integrate with CMake
.\vcpkg integrate install
```

**Option 2: Chocolatey**
```bash
choco install openssl
```

**Option 3: Manual Download**
1. Download from https://slproweb.com/products/Win32OpenSSL.html
2. Install to standard location
3. Set OPENSSL_ROOT_DIR environment variable

### Linux (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install libssl-dev
```

### Linux (Fedora/RHEL)

```bash
sudo dnf install openssl-devel
```

### macOS

```bash
brew install openssl
```

## Environment Variables

### XER_FETCH_DEPENDENCIES

Control automatic dependency fetching:

```bash
# Disable automatic fetching (use system dependencies)
cmake -DXER_FETCH_DEPENDENCIES=OFF ..
```

### ENABLE_V8

Enable/disable V8 JavaScript runtime:

```bash
# Enable V8
cmake -DENABLE_V8=ON ..

# Disable V8
cmake -DENABLE_V8=OFF ..
```

### XER_SKIP_V8_SYNC

Skip V8 source sync when already present:

```bash
# Skip sync (useful for cached builds)
cmake -DXER_SKIP_V8_SYNC=ON ..
```

### OPENSSL_ROOT_DIR

Specify OpenSSL installation location:

```bash
cmake -DOPENSSL_ROOT_DIR=/path/to/openssl ..
```

## Build Presets

### Default Preset (No V8)

```bash
cmake --preset default
cmake --build --preset default
```

### V8 Preset (No LLVM/Clang)

```bash
cmake --preset v8
cmake --build --preset v8
```

### V8 with Cached Source

```bash
cmake --preset v8 -DXER_SKIP_V8_SYNC=ON
cmake --build --preset v8
```

## Troubleshooting

### Error: XERV8SourceSync custom build failed

**Cause:** gclient sync command failed

**Solutions:**
1. Check internet connection
2. Verify depot_tools downloaded correctly
3. Try manual sync: `cd .deps && python3 depot_tools/gclient sync`
4. Use cached build: `-DXER_SKIP_V8_SYNC=ON`

### Error: ENABLE_V8=ON requires OpenSSL

**Cause:** OpenSSL not found and LLVM/Clang is available

**Solutions:**
1. Install OpenSSL (see installation instructions above)
2. Set OPENSSL_ROOT_DIR if installed in non-standard location
3. Disable LLVM/Clang (if not needed): Build without LLVM/Clang packages
4. Disable V8 if not needed: `-DENABLE_V8=OFF`

### Error: V8 not found or not properly built

**Cause:** V8 build failed or incomplete

**Solutions:**
1. Check V8 build logs in `.deps/v8-*-external`
2. Ensure Python3 is available
3. Ensure ninja is in PATH
4. Check system requirements for V8 build
5. Try clean build: remove `.deps` directory

### Error: depot_tools not found

**Cause:** depot_tools download failed

**Solutions:**
1. Check internet connection
2. Manually download depot_tools
3. Install depot_tools globally
4. Set DEPOT_TOOLS environment variable

### Slow Build Times

**Causes:**
- V8 source sync is slow
- V8 build is slow

**Solutions:**
1. Use `-DXER_SKIP_V8_SYNC=ON` for cached builds
2. Increase build jobs: `-DXER_V8_BUILD_JOBS=8`
3. Disable V8 if not needed: `-DENABLE_V8=OFF`
4. Use prebuilt V8 from system

## Build Verification

After fixing the configuration, verify the build:

```bash
# Clean build directory
rm -rf build

# Configure without LLVM/Clang (OpenSSL optional)
cmake -DENABLE_V8=ON -DXER_SKIP_V8_SYNC=ON -S . -B build

# Build
cmake --build build --config Release

# Verify
./build/out/Release/EngineBuilder --help
```

## Recommended Workflow

### First-Time Build (No LLVM/Clang)

```bash
# Configure with V8 (OpenSSL not required)
cmake -DENABLE_V8=ON -S . -B build

# Build (this will take time for V8)
cmake --build build --config Release
```

### Full Build (With LLVM/Clang)

```bash
# Install OpenSSL first (see installation instructions above)

# Configure with V8 and LLVM/Clang
cmake -DENABLE_V8=ON -S . -B build

# Build
cmake --build build --config Release
```

### Subsequent Builds (Cached)

```bash
# Configure with skip option
cmake -DENABLE_V8=ON -DXER_SKIP_V8_SYNC=ON -S . -B build

# Build (much faster)
cmake --build build --config Release
```

### Development Without V8

```bash
# Configure without V8 (fastest)
cmake -DENABLE_V8=OFF -S . -B build

# Build
cmake --build build --config Release
```

## Problem 3: Complex Dependencies (OpenSSL, libuv, FFmpeg)

**Issue:**
- OpenSSL is required by V8, network module, and libuv
- libuv is required by V8 and network module
- FFmpeg is required by media features
- Manual installation is cumbersome across platforms

### Solution

Created `cmake/deps/auto_build.cmake` with intelligent dependency management:

#### 1. vcpkg Integration (Windows)
Auto-detects vcpkg and attempts to install dependencies:

```cmake
if(WIN32 AND EXISTS "$ENV{VCPKG_ROOT}")
    vcpkg install openssl:x64-windows
    vcpkg install libuv:x64-windows
    vcpkg install ffmpeg:x64-windows
endif()
```

#### 2. libuv Auto-build
Automatically builds libuv from source if not found:
- Downloads from GitHub
- Builds using CMake
- Creates `libuv::libuv` target

#### 3. OpenSSL Handling
- Tries vcpkg first
- Falls back to warning with installation instructions
- Creates dummy target to prevent build failures

#### 4. FFmpeg Handling
- Tries vcpkg first
- Downloads source for manual build if needed
- Provides clear instructions

### Usage

Enable auto-build with:
```bash
cmake -DXER_FETCH_DEPENDENCIES=ON ..
```

With vcpkg on Windows:
```bash
# Set VCPKG_ROOT environment variable
set VCPKG_ROOT=C:\path\to\vcpkg

# Run CMake
cmake -DXER_FETCH_DEPENDENCIES=ON ..
```

## Problem 4: Compatibility Layer (compat/absl)

**Issue:**
- Project had a compatibility layer `compat/absl/` with header-only stubs
- This was a fallback when real Abseil wasn't found
- Stubs were incomplete and could cause runtime issues
- Added unnecessary complexity to the build system

### Solution

**Removed compat/absl completely:**
```bash
rm -rf compat/
```

**Updated system.cmake to require real Abseil:**
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
- ✅ Eliminates compatibility stubs
- ✅ Ensures real Abseil is always used
- ✅ Consistent behavior across all builds
- ✅ Removes unnecessary code complexity
- ✅ Auto-fetches Abseil when XER_FETCH_DEPENDENCIES=ON

## Problem 5: Build System Cleanup

**Issue:**
- Multiple overlapping dependency management files
- auto_build.cmake duplicated functionality
- External dependencies not properly integrated
- Confusing dependency resolution order

### Solution

**Removed auto_build.cmake:**
- Functionality moved to appropriate external files
- vcpkg integration integrated into system.cmake
- libuv auto-fetch integrated into system.cmake
- OpenSSL handled by openssl_external.cmake
- FFmpeg handled by ffmpeg_external.cmake

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
- Removed OpenSSL complex logic (moved to openssl_external.cmake)
- Removed FFmpeg complex logic (moved to ffmpeg_external.cmake)
- Kept libuv auto-fetch (simple case)
- Kept Abseil auto-fetch (simple case)
- Clear dependency validation

**Updated .gitignore:**
```gitignore
# Compatibility layer (removed in favor of real dependencies)
/compat/
```

## Summary of Changes

1. **CMakeLists.txt**: Added XER_SKIP_V8_SYNC option, proper external dependency inclusion
2. **v8_external.cmake**: Added cache checks for depot_tools, V8 source, and V8 builds
3. **javascript.cmake**: Added V8 availability validation
4. **system.cmake**:
   - Fixed OpenSSL dependency logic
   - Removed compat/absl fallback
   - Added mandatory Abseil requirement
   - Added libuv auto-fetch
   - Added libxml2 auto-fetch (required for V8+LLVM/Clang)
   - Added zstd auto-fetch (optional compression)
   - Added zlib auto-fetch (required compression)
   - Simplified dependency validation
5. **compat/**: Removed entire directory (11 stub header files)
6. **auto_build.cmake**: Removed (functionality moved to appropriate modules)
7. **openssl_external.cmake**: Already exists, handles OpenSSL properly
8. **ffmpeg_external.cmake**: Already exists, handles FFmpeg properly
9. **libusb_external.cmake**: Already exists, handles libusb properly
10. **.gitignore**: Added /compat/ to removed files

These changes make the build system more robust by:
- Avoiding unnecessary rebuilds
- Providing clear warnings when dependencies are missing
- Supporting cached builds for faster development
- Validating prerequisites before attempting builds
- Making OpenSSL requirement conditional on actual need (LLVM/Clang)
- Providing clear installation instructions for missing dependencies
- Auto-building libuv from source when not found
- Auto-building libxml2 from source when not found
- Auto-building zstd from source when not found
- Auto-building zlib from source when not found
- Removing compatibility stubs in favor of real dependencies
- Ensuring consistent behavior across all builds
- Simplifying dependency management
- Proper separation of concerns in build files
