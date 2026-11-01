# Audio subsystem

The `src/audio` directory contains the project’s audio pipeline and DSP-oriented functionality. It is built to support signal processing, audio file handling, effect logic, and integration with the broader runtime.

## Scope

- audio engine and core processing path
- DSP algorithms and spectral processing
- built-in effect or plugin-style processing
- analysis and metadata extraction
- file I/O for common media formats
- optional media integrations and acceleration paths

## Main areas

- `audio_core/` — main engine and source loading utilities
- `dsp_algorithms/` — FFT, filters, transforms, and processing blocks
- `effects_rack/` — effect chain and processing pipeline pieces
- `analysis_ai/` — analysis and feature extraction logic
- `file_io_codecs/` — media format handling
- `plugin_wrappers/` — plugin-host integration points

## Design goals

- modular audio processing pipeline
- hardware-aware and SIMD-friendly implementation paths
- support for async and batch-style processing
- integration with the broader runtime and I/O infrastructure

## Example

```cpp
Engine::Audio::Core::AudioSourceLoadOptions opts;
opts.input_path = "audio.mp3";
opts.target_sample_rate = 48000;
opts.target_channels = 2;

Engine::Audio::Core::AudioSourceBuffer buffer;
std::string error;
if (!Engine::Audio::Core::AudioSourceLoader::Load(opts, &buffer, &error)) {
    // handle error
}
```

## Notes

This subsystem is intentionally extensible and can serve both lightweight runtime audio tasks and heavier media-processing workflows. The exact feature set depends on the build configuration and available platform libraries.
