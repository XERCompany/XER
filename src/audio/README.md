# Audio Subsystem

Comprehensive audio processing framework with DSP algorithms, effects, analysis tools, and plugin support.

## Architecture Overview

The audio subsystem is organized into several specialized modules:

### Core Audio Engine (`audio_core/`)
- `audio_engine.*` — Main audio processing engine with callback-based pipeline
- `audio_engine_host.*` — Host integration for audio engine
- `audio_source_loader.*` — Audio file loading with format detection and decoding
- `audio_buffer_manager.*` — Buffer allocation and management
- `audio_sample_rate_converter.*` — Sample rate conversion with quality settings
- `audio_bit_depth_converter.*` — Bit depth conversion (16/24/32-bit)
- `audio_fifo_queue.*` — Thread-safe FIFO queue for audio buffers
- `audio_clock_sync.*` — Clock synchronization for multi-device setups
- `audio_latency_monitor.*` — Latency measurement and monitoring
- `audio_thread_priority.*` — Thread priority management for real-time audio

### DSP Algorithms (`dsp_algorithms/`)
- `dsp_fft_engine.*` — FFT computation with backend abstraction
- `dsp_stft_processor.*` — Short-time Fourier transform processing
- `dsp_windowing_functions.*` — Window functions (Hann, Hamming, Blackman, etc.)
- `dsp_biquad_processor.*` — Biquad filter implementation
- `dsp_fir_filter_bank.*` — FIR filter bank
- `dsp_iir_filter_bank.*` — IIR filter bank
- `dsp_convolution_engine.*` — Convolution processing
- `dsp_delay_line_circular.*` — Circular delay line
- `dsp_phase_vocoder.*` — Phase vocoder for time-stretching
- `dsp_pitch_shifter_granular.*` — Granular pitch shifting
- `dsp_spectral_shaper.*` — Spectral shaping

### Effects Rack (`effects_rack/`)
- `custom_effect.*` — Base custom effect framework
- `custom_effect_core.*` — Core effect processing
- `custom_effect_processor.*` — Effect processor implementation
- `custom_effect_eq_engine.*` — Equalizer engine
- `custom_effect_eq_lite.*` — Lightweight equalizer
- `custom_effect_invoke.*` — Effect invocation
- `custom_effect_list.*` — Effect list management
- `custom_effect_node.*` — Effect node graph
- `custom_effect_fxdata.*` — Effect data management
- `custom_effect_flow.*` — Effect flow control
- `custom_effect_package.*` — Effect packaging
- `custom_effect_struct.*` — Effect structures
- `custom_effect_accel.*` — SIMD-accelerated effects
- `custom_effect_advanced_processing.*` — Advanced processing
- `custom_effect_algorithms.*` — Effect algorithms
- `custom_effect_apply_multi.*` — Multi-application
- `custom_effect_node_curve_line.*` — Curve/line nodes
- `custom_effect_node_wrapper.*` — Node wrappers

#### Built-in Effects
- `fx_distortion_bitcrush.*` — Bitcrush distortion
- `fx_distortion_tube.*` — Tube distortion
- `fx_dynamics_compressor.*` — Compressor
- `fx_dynamics_expander.*` — Expander
- `fx_dynamics_gate.*` — Noise gate
- `fx_dynamics_limiter.*` — Limiter
- `fx_eq_graphic.*` — Graphic equalizer
- `fx_eq_parametric.*` — Parametric equalizer
- `fx_mod_chorus.*` — Chorus
- `fx_mod_flanger.*` — Flanger
- `fx_mod_phaser.*` — Phaser
- `fx_pitch_auto_tune_correct.*` — Auto-tune/correction
- `fx_reverb_algorithmic.*` — Algorithmic reverb
- `fx_reverb_convolution.*` — Convolution reverb

### Analysis AI (`analysis_ai/`)
- `audio_fft_analyzer.*` — FFT-based analysis
- `audio_pitch_detector.*` — Pitch detection
- `anal_onset_detection.*` — Onset detection
- `anal_beat_tracker.*` — Beat tracking
- `anal_spectrogram_generator.*` — Spectrogram generation
- `anal_harmonic_content.*` — Harmonic analysis
- `anal_loudness_meter_lufs.*` — LUFS loudness metering
- `anal_pitch_estimator.*` — Pitch estimation
- `anal_rms_peak_detector.*` — RMS and peak detection
- `ai_openvino_music_remark.*` — OpenVINO-based AI analysis

### File I/O Codecs (`file_io_codecs/`)
- `codec_wav_pcm.*` — WAV PCM codec
- `codec_wav_float.*` — WAV float codec
- `codec_mp3_lame.*` — MP3 encoding (LAME)
- `codec_flac_decoder.*` — FLAC decoding
- `codec_ogg_vorbis.*` — OGG Vorbis codec
- `codec_aac_adts.*` — AAC ADTS codec
- `codec_ffmpeg_decode_helper.*` — FFmpeg decode helper
- `codec_ffmpeg_encode_helper.*` — FFmpeg encode helper
- `codec_file_streamer.*` — File streaming
- `codec_metadata_id3.*` — ID3 metadata

### Custom Effects (`fx_customs/`)
- `fx_customs_effect_builder.*` — Effect builder
- `fx_customs_effect_utils.*` — Effect utilities
- `fx_customs_mastering_effects.*` — Mastering effects
- `fx_customs_presets.*` — Effect presets
- `fx_customs_spatial.*` — Spatial audio
- `fx_customs_spatial_effects.*` — Spatial effects
- `fx_customs_vintage_effects.*` — Vintage effects
- `fx_customs_vocal_effects.*` — Vocal effects

### MIDI Sequencing (`midi_sequencing/`)
- `midi_parser.*` — MIDI file parsing
- `midi_event_dispatcher.*` — Event dispatching
- `midi_clock_generator.*` — MIDI clock
- `midi_pattern_sequencer.*` — Pattern sequencing
- `midi_automation_curve.*` — Automation curves
- `midi_controller_mapping.*` — Controller mapping
- `midi_sysex_handler.*` — System exclusive handling

### Plugin Wrappers (`plugin_wrappers/`)
- `clap_host_interface.*` — CLAP plugin host
- `clap_minimal.*` — Minimal CLAP implementation
- `clap_external_plugin_host.*` — External CLAP hosting
- `vst3_host_interface.*` — VST3 plugin host
- `au_host_interface.*` — Audio Units host
- `plugin_builtin_host.*` — Built-in plugin host
- `plugin_parameter_bridge.*` — Parameter bridging

### Demo Applications (`demo/`)
- `audio_modules_smoke.*` — Module smoke tests
- `audio_analysis_smoke.*` — Analysis smoke tests
- `audio_dsp_demo.*` — DSP demonstrations
- `audio_fx_custom.*` — Custom effect demos

## Design Patterns

### Factory Pattern
- Effect creation through builders
- Codec selection based on file extension

### Strategy Pattern
- Multiple FFT backends (FFTW, KissFFT, etc.)
- Different resampling quality modes

### Chain of Responsibility
- Effect processing pipeline
- Audio signal flow through nodes

### Observer Pattern
- Audio process callbacks
- Event dispatching

## SIMD Acceleration

Most DSP algorithms support:
- **SSE2** — Basic SIMD acceleration
- **AVX2** — Advanced SIMD with 256-bit vectors
- **AVX-512** — Extended SIMD (when available)
- **Fallback** — Scalar implementations for compatibility

### Zero-Copy Optimization
- ProcessBlock/ProcessSample support zero-copy when input == output
- Reduces memory bandwidth
- Improves cache efficiency

## Async Processing

All effects and filters support async operations:
- `ProcessBlockAsync()` — Asynchronous block processing
- `std::future` — Future-based async API
- Thread pool integration with async_io module

## Memory Management

### Buffer Pooling
- Reusable buffer pools for audio data
- Reduces allocation overhead
- Improves real-time performance

### Memory Statistics
- `GetMemoryStats()` method for memory reporting
- Tracks buffer allocations
- Monitors memory usage

## Namespace Organization

Primary namespace: `Engine::Audio`

Sub-namespaces:
- `Engine::Audio::Core` — Core audio engine
- `Engine::Audio::DSP` — DSP algorithms
- `Engine::Audio::Demo` — Demo applications

## Usage Examples

### Audio Loading
```cpp
Engine::Audio::Core::AudioSourceLoadOptions opts;
opts.input_path = "audio.mp3";
opts.target_sample_rate = 48000;
opts.target_channels = 2;

Engine::Audio::Core::AudioSourceBuffer buffer;
std::string error;
if (!Engine::Audio::Core::AudioSourceLoader::Load(opts, &buffer, &error)) {
    // Handle error
}
```

### Audio Engine
```cpp
Engine::Audio::Core::AudioEngine engine;
engine.Initialize(48000, 512, 2);
engine.SetProcessCallback([](float** inputs, float** outputs, int channels, size_t frames) {
    // Process audio
});
engine.Start();
```

### FFT Processing
```cpp
Engine::Audio::DSP::FFTEngine fft(1024);
fft.Forward(audio_buffer, 1024);
auto spectrum = fft.GetSpectrum();
fft.Inverse(output_buffer, 1024);
```

### Effect Processing
```cpp
CustomEffect effect;
effect.Initialize(sample_rate, buffer_size);
effect.ProcessBlock(input_buffer, output_buffer, frame_count);
```

## Performance Considerations

### SIMD Detection
Runtime detection of CPU capabilities:
- SSE2 check
- AVX2 check
- AVX-512 check
- Automatic fallback to scalar

### Thread Pool Integration
- Multi-threaded processing for large blocks
- Parallel effect chains
- Async I/O integration

### Cache Optimization
- Buffer alignment for SIMD
- Stride-aware memory access
- Prefetching for large buffers

## Known Issues and Improvements

### High Priority
1. **Documentation**: Add detailed API documentation to all headers
2. **Unit Tests**: Add comprehensive unit tests for DSP algorithms
3. **Error Handling**: Standardize error reporting across components
4. **Memory Leaks**: Verify no memory leaks in effect chains

### Medium Priority
5. **Plugin System**: Improve plugin loading and management
6. **Real-time Safety**: Ensure all paths are real-time safe
7. **Parameter Automation**: Add automation support for effect parameters
8. **Presets**: Expand preset library for built-in effects

### Low Priority
9. **GPU Acceleration**: Consider CUDA/OpenCL for heavy DSP
10. **Machine Learning**: Integrate ML-based effects
11. **Spatial Audio**: Enhance spatial audio capabilities
12. **Network Audio**: Add network audio streaming support

## Dependencies

- **FFmpeg** — Audio/video codec support
- **FFTW** — FFT computation (optional, has fallback)
- **LAME** — MP3 encoding (optional)
- **FLAC** — FLAC codec (optional)
- **Standard Library** — All core functionality

## Build Configuration

Components are conditionally compiled based on:
- `ENABLE_MEDIA` — Enables media dependencies (FFmpeg, etc.)
- SIMD support is auto-detected at runtime
- Plugin support requires platform-specific SDKs

## Testing

### Unit Tests
Located in `tests/`:
- `dsp_memory_async_zero_copy_test.cc` — DSP memory and async tests
- Zero-copy verification
- SIMD path testing
- Memory statistics validation

### Smoke Tests
Located in `demo_app/`:
- `audio_modules_smoke.js` — Module smoke test
- `audio_analysis_smoke.cc` — Analysis smoke test
- `audio_dsp_demo.cc` — DSP demonstration

## Self-Test

Audio source loader includes self-test:
```cpp
std::string report;
bool ok = Engine::Audio::Core::AudioSourceLoader::SelfTest(&report);
std::cout << report;
```

## TODO

### Audio Subsystem
- Implement full mpg123 fallback for MP3 decode
- Add more unit tests (valid MP3, WAV, error cases)
- Integrate async decode in effect chains
- Advanced error propagation and profiling
- Add more built-in effects
- Improve plugin compatibility
- Add MIDI learn functionality
- Implement effect presets system
