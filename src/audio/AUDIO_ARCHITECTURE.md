# Audio Subsystem Architecture v2.0

## Overview

The audio subsystem provides comprehensive audio processing capabilities including DSP algorithms, effects, analysis tools, and plugin support. This document describes the updated architecture with async I/O, Flux logging, and cache integration.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    Audio Application Layer                  │
│  (Audio Engine, Effects Rack, MIDI Sequencing, Plugins)    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Audio Core Layer                         │
│  (AudioEngine, BufferManager, SampleRateConverter, etc.)    │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    DSP Layer                                │
│  (FFT, Filters, Convolution, Spectral Processing)          │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                    Infrastructure Layer                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │  Async I/O   │  │  Flux Log    │  │    Cache     │     │
│  │  Integration │  │  Integration │  │  Integration │     │
│  └──────────────┘  └──────────────┘  └──────────────┘     │
└─────────────────────────────────────────────────────────────┘
```

## Key Components

### 1. Audio Core (`audio_core/`)

#### AudioEngine
- Main audio processing engine with callback-based pipeline
- **New**: Async I/O integration for async audio processing
- **New**: Flux logging for engine events and errors
- **New**: Cache integration for audio file metadata and presets

#### AudioSourceLoader
- Audio file loading with format detection and decoding
- **New**: Async file loading via AsyncOperationManager
- **New**: Cache integration for decoded audio buffers
- **New**: Flux logging for load operations

#### AudioBufferManager
- Buffer allocation and management
- **New**: Cache integration for buffer pooling
- **New**: Memory statistics via Flux logging

#### AudioSampleRateConverter
- Sample rate conversion with quality settings
- **New**: Async conversion support
- **New**: SIMD acceleration with runtime detection

#### AudioFifoQueue
- Thread-safe FIFO queue for audio buffers
- **New**: Integration with IOThreadPool for async operations

### 2. DSP Algorithms (`dsp_algorithms/`)

#### FFTEngine
- FFT computation with backend abstraction
- **New**: Async FFT processing via thread pool
- **New**: Cache integration for FFT plans (FFTW)
- **New**: Flux logging for FFT operations

#### BiquadProcessor
- Biquad filter implementation
- **New**: SIMD acceleration (SSE2/AVX2/AVX-512)
- **New**: Async filter processing

#### ConvolutionEngine
- Convolution processing
- **New**: Cache integration for impulse responses
- **New**: Async convolution for large impulse responses

#### PhaseVocoder
- Phase vocoder for time-stretching
- **New**: Async STFT processing
- **New**: Cache integration for spectral data

### 3. Effects Rack (`effects_rack/`)

#### CustomEffect
- Base custom effect framework
- **New**: Async effect processing
- **New**: Flux logging for effect events
- **New**: Cache integration for effect presets

#### CustomEffectCore
- Core effect processing
- **New**: SIMD-accelerated processing
- **New**: Async block processing

#### Built-in Effects
- All effects support async processing
- Cache integration for effect parameters and presets
- Flux logging for effect state changes

### 4. Analysis AI (`analysis_ai/`)

#### AudioFFTAnalyzer
- FFT-based analysis
- **New**: Async analysis via thread pool
- **New**: Cache integration for analysis results

#### BeatTracker
- Beat tracking
- **New**: Async beat detection
- **New**: Cache integration for beat grids

#### SpectrogramGenerator
- Spectrogram generation
- **New**: Async generation
- **New**: Cache integration for spectrogram data

## Infrastructure Integration

### Async I/O Integration

#### AudioAsyncManager
New component: `audio/audio_core/audio_async_manager.h/cc`

```cpp
namespace Engine::Audio::Core {

class AudioAsyncManager {
public:
    static AudioAsyncManager& Instance();

    // Async audio operations
    void EnqueueAudioOperation(std::function<void()> operation);
    std::future<void> ProcessAudioAsync(float* input, float* output, size_t frames);

    // Async file loading
    std::future<AudioSourceBuffer> LoadAudioFileAsync(const std::string& path);

    // Async analysis
    std::future<AnalysisResult> AnalyzeAudioAsync(const float* audio, size_t frames);

private:
    IO::AsyncIO::IOThreadPool& thread_pool_;
};

}  // namespace Engine::Audio::Core
```

**Integration Points:**
- AudioSourceLoader::LoadAsync()
- CustomEffect::ProcessBlockAsync()
- FFTAnalyzer::AnalyzeAsync()
- SpectrogramGenerator::GenerateAsync()

### Flux Logging Integration

#### AudioLogger
New component: `audio/audio_core/audio_logger.h/cc`

```cpp
namespace Engine::Audio::Core {

class AudioLogger {
public:
    static AudioLogger& Instance();

    // Audio engine events
    void LogEngineStart(int sample_rate, int buffer_size);
    void LogEngineStop();
    void LogEngineError(const std::string& error);

    // Effect events
    void LogEffectLoad(const std::string& effect_name);
    void LogEffectProcess(const std::string& effect_name, double cpu_time);
    void LogEffectError(const std::string& effect_name, const std::string& error);

    // Analysis events
    void LogAnalysisStart(const std::string& analysis_type);
    void LogAnalysisComplete(const std::string& analysis_type, double duration);
    void LogAnalysisError(const std::string& analysis_type, const std::string& error);

    // File I/O events
    void LogFileLoad(const std::string& path, size_t size_ms);
    void LogFileSave(const std::string& path, size_t size_ms);
    void LogFileError(const std::string& path, const std::string& error);

private:
    flux::core::Logger& logger_;
};

}  // namespace Engine::Audio::Core
```

**Integration Points:**
- AudioEngine::Start/Stop
- AudioSourceLoader::Load
- CustomEffect::ProcessBlock
- All analysis tools
- File codecs

### Cache Integration

#### AudioCacheManager
New component: `audio/audio_core/audio_cache_manager.h/cc`

```cpp
namespace Engine::Audio::Core {

// Audio file metadata cache entry
struct AudioFileMetadata {
    int sample_rate;
    int channels;
    int bit_depth;
    size_t duration_samples;
    double duration_seconds;
    std::string format;
};

// Effect preset cache entry
struct EffectPreset {
    std::string effect_name;
    std::vector<uint8_t> parameters;
    std::chrono::system_clock::time_point cached_at;
};

// Analysis result cache entry
struct AnalysisResultCache {
    std::string analysis_type;
    std::string audio_hash;  // Hash of audio content
    std::vector<uint8_t> result_data;
    std::chrono::system_clock::time_point cached_at;
};

class AudioCacheManager {
public:
    static AudioCacheManager& Instance();

    // Audio file metadata cache
    bool CacheAudioMetadata(const std::string& path,
                           const AudioFileMetadata& metadata,
                           std::string* error);
    std::optional<AudioFileMetadata> GetCachedMetadata(const std::string& path,
                                                      std::string* error);

    // Effect preset cache
    bool CacheEffectPreset(const std::string& preset_name,
                         const EffectPreset& preset,
                         std::string* error);
    std::optional<EffectPreset> GetCachedPreset(const std::string& preset_name,
                                                std::string* error);

    // Analysis result cache
    bool CacheAnalysisResult(const std::string& analysis_type,
                            const std::string& audio_hash,
                            const AnalysisResultCache& result,
                            std::string* error);
    std::optional<AnalysisResultCache> GetCachedAnalysisResult(
        const std::string& analysis_type,
        const std::string& audio_hash,
        std::string* error);

    // Cache management
    void ClearMetadataCache();
    void ClearPresetCache();
    void ClearAnalysisCache();

private:
    Engine::Cache::CacheManager& cache_manager_;
};

}  // namespace Engine::Audio::Core
```

**Integration Points:**
- AudioSourceLoader::Load (cache metadata)
- CustomEffect::LoadPreset (cache presets)
- All analysis tools (cache results)

## SIMD Acceleration Strategy

### Detection
Runtime CPU capability detection:
- SSE2 support check
- AVX2 support check
- AVX-512 support check
- Automatic fallback to scalar

### Implementation
- Multiple function variants: `_scalar`, `_sse2`, `_avx2`, `_avx512`
- Function pointer selection at initialization
- Zero-copy optimizations where possible

### Coverage
- All DSP algorithms
- All effect processors
- Sample rate conversion
- Buffer operations

## Thread Safety

### Real-Time Safety
- No memory allocations in audio callback
- Lock-free algorithms where possible
- Predictable execution time
- Priority inheritance for audio threads

### Async Processing
- Thread pool for non-real-time operations
- Separate audio processing thread
- Proper synchronization with mutexes
- Wait-free queues for audio data

## Performance Optimization

### Memory Management
- Buffer pooling to reduce allocations
- Memory-aligned buffers for SIMD
- Zero-copy operations where possible
- Cache-aware memory access patterns

### CPU Utilization
- SIMD for parallel processing
- Multi-threading for large blocks
- Cache prefetching
- Branch prediction optimization

### Latency
- Low-latency FIFO queues
- Minimize lock contention
- Real-time priority threads
- Hardware-specific optimizations

## Namespace Organization

Primary namespace: `Engine::Audio`

Sub-namespaces:
- `Engine::Audio::Core` — Core audio engine
- `Engine::Audio::DSP` — DSP algorithms
- `Engine::Audio::Effects` — Effects rack
- `Engine::Audio::Analysis` — Analysis tools
- `Engine::Audio::MIDI` — MIDI sequencing
- `Engine::Audio::Codecs` — File I/O codecs
- `Engine::Audio::Plugins` — Plugin wrappers

## Build Configuration

### New Dependencies
- XER_AsyncIO (async I/O operations)
- XER_Cache (caching)
- XER_Flux (logging)
- Existing: FFmpeg, FFTW, etc.

### CMake Updates
```cmake
# Link with async I/O
target_link_libraries(XER_Audio_Core PUBLIC XER_AsyncIO)

# Link with cache
target_link_libraries(XER_Audio_Core PUBLIC XER_Cache)

# Link with Flux
target_link_libraries(XER_Audio_Core PUBLIC XER_Flux)
```

## Migration Path

### Phase 1: Infrastructure
1. Create AudioAsyncManager
2. Create AudioLogger
3. Create AudioCacheManager
4. Update CMakeLists.txt

### Phase 2: Core Integration
1. Update AudioEngine with async/log/cache
2. Update AudioSourceLoader with async/log/cache
3. Update AudioBufferManager with cache
4. Update AudioFifoQueue with async

### Phase 3: DSP Integration
1. Update FFTEngine with async/cache
2. Update BiquadProcessor with async
3. Update ConvolutionEngine with cache
4. Update PhaseVocoder with async

### Phase 4: Effects Integration
1. Update CustomEffect with async/log/cache
2. Update built-in effects with async
3. Add preset caching
4. Add effect logging

### Phase 5: Analysis Integration
1. Update analysis tools with async
2. Add result caching
3. Add analysis logging
4. Optimize parallel processing

## Testing

### Unit Tests
- Async operation tests
- Cache integration tests
- Logging verification tests
- SIMD path tests

### Smoke Tests
- Audio engine start/stop
- File loading with cache
- Effect processing with async
- Analysis with cache

### Performance Tests
- Latency measurements
- CPU utilization profiling
- Memory usage tracking
- SIMD vs scalar comparison

## Documentation Updates

### README.md
- Update architecture section
- Add async I/O examples
- Add cache usage examples
- Add logging configuration

### API Documentation
- Document new async methods
- Document cache keys and TTL
- Document logging levels
- Document SIMD fallback paths

## Backward Compatibility

- All existing synchronous APIs remain
- Async APIs are additive
- Cache is optional (can be disabled)
- Logging can be configured
- SIMD fallback ensures compatibility

## Future Enhancements

### GPU Acceleration
- CUDA/OpenCL for heavy DSP
- GPU-accelerated convolution
- GPU-accelerated FFT

### Machine Learning
- ML-based effects
- Neural audio processing
- Real-time audio classification

### Network Audio
- Network audio streaming
- Remote audio processing
- Distributed audio effects
