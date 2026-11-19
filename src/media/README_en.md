# Media pipeline

[English](README_en.md) | [Русский](README_ru.md)

The `src/media` directory contains the project's modular media playback
and processing stack (`XER::Media::*`). It implements the pipeline
described in the media architecture document: data sources → demuxers →
decoders → renderers → sinks, with player orchestration, session/service
layers, muxers, and routing.

## Layout

| Submodule | Purpose |
|---|---|
| `base/` | `SeekableBuffer`, media logging, time utilities |
| `demuxer/` | `DataSource` interface; `FileDataSource`; `UrlDataSource` (HTTP range requests via `src/content/network`); `BufferedDataSource` (byte-window read-ahead cache over any source); `MediaSource`/`ChunkDemuxer` (MSE-style append / end-of-stream); FFmpeg demuxer bridge |
| `decoders/` | Audio/video decoder interfaces + FFmpeg adapters. The full FFmpeg static closure is packaged in `YA.MediaDecoderUniversal.dll` (ggml/whisper/shaderc/vulkan delay-loaded) |
| `renderers/` | `AudioRendererImpl`, `VideoRendererImpl`, audio-clock sync, frame compositor |
| `sinks/` | `WasapiOutputStream`, `FrameCacheSink`, device-thread pull model |
| `muxers/` | Container muxers (EBML/MP4 family) |
| `player/` | `MediaPlayer`/`MediaPlayerImpl` — pipeline orchestration |
| `session/` | Media session state |
| `service/` | Out-of-process media service |
| `router/` | Media routing between sources, renderers, sinks |
| `gpu/` | GPU interop |
| `capture/` | Capture devices |
| `cdm/` | Content decryption module stubs |
| `filters/` | Filter graph |
| `metrics/` | Pipeline metrics |

## Data sources

```cpp
// HTTP source with range requests and unknown-size streaming fallback.
XER::Media::UrlDataSource url;
url.Open("https://example.com/movie.mp4");

// Byte-window read-ahead cache over any DataSource.
XER::Media::BufferedDataSource buffered(&url, forward_bytes, backward_bytes);
buffered.ReadAt(offset, dst, size);      // cache hit → no source I/O
```

- `UrlDataSource` probes size via `HEAD`, falls back to a byte-range
  `GET`, and streams when the size is unknown.
- `BufferedDataSource` keeps a `[window_start_, +size)` window with
  `backward_bytes` of history, forward-fills up to `forward_bytes`, and
  falls back to direct `ReadAt` for out-of-window accesses.

## Packaging

Sources are recursively globbed by `src/media/CMakeLists.txt` and
partitioned into YA shared modules: `YA.MediaCore`, `YA.MediaPipeline`,
`YA.MediaAudio`, `YA.MediaRouter`, `YA.MediaMuxer`, `YA.MediaCapture`,
`YA.MediaGpu`, `YA.MediaDecoderUniversal`, `YA.MediaDecoder`,
`YA.MediaDecoderZero`, `YA.MediaEncoder`, `YA.DesginCSS` (UI CSS engine).

## Tests

`tests/media_smoke.cc` → `XER_Media_Smoke.exe`: demux/decode/render
pipeline, `BufferedDataSource` (forward read, backward seek, out-of-range),
CSS matching/cascade/invalidation, stylesheet parser. Run:

```bat
bin\XER_Media_Smoke.exe    :: works without MSYS2 DLLs on PATH
```
