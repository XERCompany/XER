# src

This directory contains the core implementation of XER and its main runtime subsystems.

## Main areas

- `async_io/` — asynchronous I/O, buffer pooling, scheduling, and hardware abstraction
- `audio/` — audio engine, DSP features, and media-related processing modules
- `cache/` — caching utilities and persistence helpers
- `content/` — content-centric subsystems such as network, AI, and model integrations
- `flux/` — terminal and rendering runtime components
- `modules/` — JavaScript-facing modules exposed to the runtime
- `app_command.*` — command-line parsing and application entry helpers

## High-level responsibilities

- runtime orchestration and dynamic compilation
- native execution and bridge integration with the JavaScript environment
- module registration and runtime plumbing
- low-level I/O, networking, buffering, and hardware interaction
- audio processing and DSP-related features
- AI and model-oriented backends with optional integrations

## Notes

The project is intentionally modular. Many capabilities live in dedicated subdirectories instead of a single monolithic runtime. This keeps the codebase easier to evolve while supporting optional features and platform-specific integrations.
