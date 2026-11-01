# src

This directory contains the core implementation of XER and its main runtime subsystems.

## Main areas

- `async_io/` — asynchronous I/O, buffer pooling, queues, and hardware capability layers
- `audio/` — audio engine, DSP features, and audio plugins or examples
- `cache/` — cache management utilities and persistence helpers
- `content/` — content-centric subsystems such as network, AI, and model integrations
- `flux/` — terminal and rendering runtime components
- `modules/` — JavaScript-facing module layer exposed to the runtime
- `app_command.*` — command-line interface and app entry helpers

## High-level responsibilities

- runtime orchestration and dynamic compilation
- native execution and bridge integration with the V8/JS environment
- module registration and runtime plumbing
- I/O, networking, and buffering primitives
- audio processing and DSP-oriented features
- model/AI-related backends and optional integrations

## Notes

The project is intentionally modular. Many capabilities live in dedicated subdirectories rather than a single monolithic runtime. This keeps the codebase easier to evolve while supporting optional features and platform-specific integrations.
