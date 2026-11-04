# XER Native Node.js Bindings

Node.js native bindings for the XER C++ runtime using Nan.

## Overview

This module provides JavaScript access to XER's C++ subsystems:
- **Helper**: Buffer utilities, hex encoding/decoding
- **Cache**: Persistent and temporary storage
- **Audio**: Audio engine, DSP, effects, analysis
- **Async IO**: Async file I/O, thread pool, hardware abstraction
- **Flux**: Terminal UI, logging, module system

## Installation

```bash
cd package/module
npm install
```

This will compile the native module using cmake-js.

### Build Commands

```bash
# Configure CMake
npm run configure

# Build
npm run build

# Clean build
npm run clean

# Rebuild
npm run build
```

## Current Status

**This is a stub implementation** showing the API structure. To fully implement all 583+ APIs:

1. Uncomment the XER includes in `src/binding.cc`
2. Link against the XER C++ libraries
3. Replace stub implementations with actual XER calls
4. Update `binding.gyp` to include all source files

## API Documentation

### Helper Module

Buffer utilities for binary data manipulation.

```javascript
const { helper } = require('xer-native');

// String to buffer
const buffer = helper.buffer.fromString('Hello World');

// Buffer to string
const str = helper.buffer.toString(buffer);

// Hex encoding
const hex = helper.buffer.hexEncode(buffer);
console.log(hex); // "48656c6c6f20576f726c64"

// Hex decoding
const decoded = helper.buffer.hexDecode(hex);
```

### Cache Module

Persistent and temporary storage for text and binary data.

```javascript
const { cache } = require('xer-native');

// Get cache instance
const instance = cache.instance();

// Write persistent text
cache.writePersistentText('scope', 'key', 'name', 'content');

// Read persistent text
const content = cache.readPersistentText('scope', 'key', 'name');
```

### Audio Module

Audio engine for real-time audio processing.

```javascript
const { audio } = require('xer-native');

// Create audio engine
const engine = new audio.Engine();

// Initialize with sample rate, buffer size, and channel count
engine.initialize(48000, 512, 2);

// Start the engine
engine.start();

// Get engine properties
console.log('Sample Rate:', engine.sampleRate);
console.log('Buffer Size:', engine.bufferSize);
console.log('Channel Count:', engine.channelCount);
console.log('CPU Load:', engine.cpuLoad);

// Stop the engine
engine.stop();
```

### Async IO Module

Asynchronous file I/O operations.

```javascript
const { asyncIO } = require('xer-native');

// Create file reader
const reader = new asyncIO.FileReader();

// Open file
reader.openFile('/path/to/file.bin');

// Check if file is open
if (reader.isFileOpen()) {
  console.log('File size:', reader.fileSize);

  // Read synchronously
  const data = reader.readSync(1024);

  // Seek to position
  reader.seek(0);

  // Get current position
  console.log('Position:', reader.position);
}

// Close file
reader.closeFile();

// Check for errors
if (reader.lastError) {
  console.error('Error:', reader.lastError);
}
```

### Flux Module

Terminal UI and core runtime management.

```javascript
const { flux } = require('xer-native');

// Create flux core
const core = new flux.Core();

// Initialize core
core.initialize();

// Check if running
if (core.isRunning) {
  console.log('Flux core is running');
}

// Get terminal size
const size = flux.terminal.defaultSize();
console.log('Terminal:', size.columns, 'x', size.rows);

// Detect actual terminal size
const detected = flux.terminal.detectSize();
console.log('Detected:', detected.columns, 'x', detected.rows);

// Shutdown core
core.shutdown();
```

## Full Implementation Guide

To implement all 583+ APIs from the XER subsystems:

### Step 1: Update CMakeLists.txt

Add all source files from XER that you want to link:

```cmake
set(SOURCES
    src/binding.cc
    ../../src/helper/buffer.cc
    ../../src/helper/dict.cc
    ../../src/async_io/async_file_reader.cc
    ../../src/async_io/async_file_writer.cc
    # ... add all required source files
)

# Link against XER libraries
target_link_libraries(xer_native PRIVATE
    # Add XER libraries here
)
```

### Step 2: Update binding.cc

Uncomment the includes and link against XER classes:

```cpp
#include "src/helper/buffer.h"
#include "src/cache/cache_manager.h"
#include "src/audio/audio_core/audio_engine.h"
#include "src/async_io/async_file_reader.h"
#include "src/flux/core/flux_core.h"
```

Replace stub implementations with actual XER calls:

```cpp
NAN_METHOD(AudioEngineInitialize) {
  Nan::HandleScope scope;
  Local<Object> obj = info[0]->ToObject();
  auto* engine = static_cast<Engine::Audio::Core::AudioEngine*>(
      Local<External>::Cast(obj->Get(Nan::New<String>("ptr").ToLocalChecked()))->Value());
  int sample_rate = info[1]->Int32Value();
  int buffer_size = info[2]->Int32Value();
  int channel_count = info[3]->Int32Value();
  bool success = engine->Initialize(sample_rate, buffer_size, channel_count);
  info.GetReturnValue().Set(Nan::New<Boolean>(success));
}
```

### Step 3: Implement All APIs

Based on the API inventory (see `API_INVENTORY.md`), implement bindings for:

1. **Helper** (24 methods)
   - Buffer utilities
   - Dict operations
   - JSON helpers
   - Compression
   - String utilities

2. **Async IO** (~150 methods)
   - File I/O (AsyncFileReader, AsyncFileWriter)
   - Thread pool (IOThreadPool)
   - Buffer pools (AsyncBufferPool, IOBufferPool)
   - Hardware abstraction (CPU, GPU, memory, disk, network)
   - Synchronization primitives (Mutex, Barrier, Semaphore)
   - Logging and debugging

3. **Cache** (~40 methods)
   - CacheManager
   - PersistentStorage
   - TemporaryStorage
   - Encryption handler

4. **Audio** (~200 methods)
   - AudioCore (Engine, Buffer, AsyncManager, etc.)
   - DSP algorithms (FFT, Biquad, Convolution)
   - Effects (CustomEffectFlow, Processor)
   - Analysis (FFT analyzer, Pitch detector)
   - File codecs (WAV, MP3, FLAC, etc.)

5. **Flux** (~30 methods)
   - FluxCore, FluxContext
   - ModuleManager, Logger
   - Terminal emulator, Window
   - Console utilities

### Step 4: Memory Management

For class wrappers:
- Use `Nan::New<External>()` to wrap C++ pointers
- Register weak cleanup with GC
- Implement proper destructors

Example:

```cpp
class AudioEngineWrapper {
public:
  static NAN_METHOD(New) {
    auto* engine = new Engine::Audio::Core::AudioEngine();
    auto* wrapper = new AudioEngineWrapper(engine);
    wrapper->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  }

  static NAN_METHOD(Initialize) {
    auto* wrapper = Unwrap<AudioEngineWrapper>(info.This());
    // ... implementation
  }

  ~AudioEngineWrapper() {
    delete engine_;
  }

private:
  Engine::Audio::Core::AudioEngine* engine_;
};
```

### Step 5: Async Operations

For async operations, use `Nan::AsyncWorker`:

```cpp
class AsyncReadWorker : public Nan::AsyncWorker {
public:
  AsyncReadWorker(Nan::Callback* callback, AsyncFileReader* reader, size_t bytes)
    : Nan::AsyncWorker(callback), reader_(reader), bytes_(bytes) {}

  void Execute() override {
    // Run on worker thread
    success_ = reader_->ReadSync(bytes_, buffer_);
  }

  void HandleOKCallback() override {
    Nan::HandleScope scope;
    Local<Value> argv[] = {
      Nan::Null(),
      Nan::CopyBuffer((char*)buffer_.data(), buffer_.size()).ToLocalChecked()
    };
    callback->Call(2, argv);
  }

private:
  AsyncFileReader* reader_;
  size_t bytes_;
  bool success_;
  std::vector<uint8_t> buffer_;
};
```

## Testing

```bash
npm test
```

Or run manually:

```bash
node test.js
```

## API Inventory

See `API_INVENTORY.md` for the complete list of 583+ APIs across all subsystems:
- 82 classes
- 35 structs
- 88 free functions
- 18 enums
- ~450 public methods

## Building

### Debug Build

```bash
cmake-js configure -DCMAKE_BUILD_TYPE=Debug
cmake-js build
```

### Release Build

```bash
cmake-js configure -DCMAKE_BUILD_TYPE=Release
cmake-js build
```

### Clean

```bash
cmake-js clean
```

## Troubleshooting

### Build Errors

If you get include errors, ensure:
1. XER source files are accessible
2. Include paths in `binding.gyp` are correct
3. All dependencies (vcpkg) are installed

### Link Errors

If you get undefined reference errors:
1. Ensure XER libraries are built
2. Add library paths to `binding.gyp`
3. Link all required dependencies (FFmpeg, etc.)

## License

MIT

## Contributing

When adding new bindings:
1. Follow the existing pattern
2. Add to `index.js` wrapper
3. Update this README
4. Add tests in `test.js`
