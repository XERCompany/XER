# XER Project Guidelines for AI Agents

This document provides specific guidelines for AI agents working on the XER codebase.

## Project Overview

XER is a high-performance C++/V8 runtime builder with multimedia and GPU acceleration. It combines JavaScript-driven build flows with dynamic native compilation.

## Key Technical Details

### Build System
- **CMake Version**: 3.21+
- **C++ Standard**: C++23
- **Primary Build Tool**: CMake with presets
- **Dependency Management**: vcpkg (C:/vcpkg) with x64-windows-release triplet
- **V8 Fetching**: Separate XER_FETCH_V8 option for V8 source build
- **Automatic Dependency Fetching**: Disabled by default (XER_FETCH_DEPENDENCIES=OFF)

### Core Technologies
- **V8**: JavaScript engine (when ENABLE_V8=ON)
- **LLVM/Clang**: Dynamic compilation (when XER_BUILD_LLVM=ON)
- **FFmpeg**: Audio/video processing
- **OpenCV**: Computer vision (optional)
- **CUDA/cuDNN**: GPU computing (optional)
- **Skia**: 2D graphics
- **SQLite**: Database support

### Architecture Patterns

#### Module System
- JavaScript modules are in `src/modules/`
- Modules are loaded via `ImportModule("...")`
- Each module has a clear interface and implementation

#### Async I/O
- Async operations use `std::future` and thread pools
- Hardware abstraction layer in `src/async_io/hardware_abstraction/`
- Thread synchronization primitives in `src/async_io/sync_primitives/`

#### Audio Subsystem
- Core audio engine in `src/audio/audio_core/`
- DSP algorithms in `src/audio/dsp_algorithms/`
- Effects in `src/audio/effects_rack/`
- Analysis tools in `src/audio/analysis_ai/`

## Coding Standards

### C++ Conventions
- Use C++23 features where appropriate
- Follow existing naming patterns (snake_case for files, camelCase/ PascalCase for code)
- Prefer const correctness
- Use RAII for resource management
- Avoid exceptions in performance-critical paths

### Code Organization
- Header files (.h) for declarations
- Implementation files (.cc) for definitions
- Separate interface from implementation
- Keep functions focused and small

### Error Handling
- Use error codes and enums for error reporting
- Provide error messages with context
- Avoid silent failures
- Log errors appropriately

## Build and Testing

### Build Commands
```bash
# Default build with media support
cmake --preset default
cmake --build --preset default

# V8 and JavaScript runtime
cmake --preset v8
cmake --build --preset v8

# Full XER with V8
cmake --preset v8
cmake --build --preset v8-xer
```

### Testing
- Unit tests in `tests/` directory
- Smoke tests in `demo_app/`
- Run smoke tests before committing
- Test audio processing with actual audio files

### Verification Steps
1. Build succeeds without warnings
2. All smoke tests pass
3. Audio processing produces valid output
4. Memory usage is reasonable
5. No memory leaks (use valgrind/sanitizers)

## Common Tasks

### Adding a New Audio Effect
1. Create header in `src/audio/effects_rack/`
2. Create implementation in `src/audio/effects_rack/`
3. Add to CMake targets in `cmake/targets/bridges.cmake`
4. Add smoke test in `demo_app/`
5. Update documentation

### Adding a New JavaScript Module
1. Create module in `src/modules/`
2. Implement module interface
3. Register in module loader
4. Add smoke test in `demo_app/`
5. Update module documentation

### Modifying Build Configuration
1. Edit appropriate CMake file in `cmake/`
2. Test with all preset configurations
3. Verify dependency resolution
4. Update build instructions in README

## Performance Considerations

### Audio Processing
- Use SIMD (SSE2/AVX2) for audio blocks
- Prefer zero-copy operations
- Use thread pools for parallel processing
- Profile with ScopedTimer

### Memory Management
- Reuse buffers when possible
- Use object pools for frequent allocations
- Monitor memory with GetMemoryStats()
- Avoid excessive allocations in hot paths

### Async Operations
- Use std::future for async APIs
- Implement proper cancellation
- Handle timeouts appropriately
- Avoid blocking in async contexts

## Dependency Management

### vcpkg Integration
- **vcpkg Path**: C:/vcpkg
- **Triplet**: x64-windows-release
- **Toolchain**: C:/vcpkg/scripts/buildsystems/vcpkg.cmake
- **Installation**: Manual via `vcpkg install <package>:x64-windows-release`

### Currently Used vcpkg Libraries
- libuv - Async I/O
- absl - Google Abseil (strings, sync, container)
- fmt - String formatting
- jsoncpp - JSON handling
- re2 - Regular expressions
- sqlite3 - Database
- zlib, zstd, brotli - Compression
- OpenSSL, curl - Networking (optional)
- FFmpeg, SDL2 - Media (optional)
- OpenCV - Computer vision (optional)
- pthreadpool, fxdiv - Parallel processing

### Adding New Dependencies
1. Install via vcpkg: `vcpkg install <package>:x64-windows-release`
2. Add find_package() to CMakeLists.txt
3. Link target with appropriate library
4. Update documentation
5. Test on target platform

### Version Pinning
- vcpkg handles versioning via manifest or latest stable
- Document tested versions in VCPKG_INTEGRATION.md
- Test updates thoroughly before integration

## Documentation

### Code Comments
- Document public APIs
- Explain non-obvious algorithms
- Comment complex logic
- Keep comments up to date

### README Updates
- Update build instructions when changing CMake
- Document new features
- Update examples
- Maintain clear structure

## Platform-Specific Notes

### Windows
- MSVC compiler required
- Git Bash/MSYS2 for some dependencies
- FFmpeg may need special handling
- Use proper path separators

### Linux
- GCC or Clang supported
- System packages via apt/yum
- pkg-config for detection
- Standard filesystem layout

### macOS
- Clang supported
- Homebrew for dependencies
- Xcode command line tools
- macOS-specific frameworks

## Safety and Security

### Runtime Safety
- Use RuntimeSafety::BootstrapStartup
- Implement integrity checks
- Validate user inputs
- Sanitize file paths

### Sandboxing
- Respect sandbox flags
- Limit file system access
- Restrict network operations
- Validate all external data

## Common Pitfalls

### Audio Processing
- Forgetting to handle sample rate conversion
- Ignoring channel count mismatches
- Not checking for buffer overflows
- Missing SIMD fallback paths

### Async I/O
- Blocking in async callbacks
- Not handling cancellation
- Race conditions with shared state
- Resource leaks on errors

### Build System
- Hardcoding paths
- Not checking for dependency availability
- Missing preset configurations
- Incomplete dependency chains

## When to Ask for Help

- Before major architectural changes
- When adding new dependencies
- For performance-critical code
- When modifying core runtime
- For security-related changes
- When build system changes are complex

## Verification Checklist

Before marking a task complete:
- [ ] Code compiles without warnings
- [ ] All existing tests pass
- [ ] New tests added if appropriate
- [ ] Documentation updated
- [ ] Build instructions verified
- [ ] No memory leaks detected
- [ ] Performance not degraded
- [ ] Code follows project conventions
