# Demo app and smoke tests

This directory contains sample scripts, runtime validation entry points, and smoke tests used to exercise XER modules and integrations.

## Goals

- validate core engine behavior
- verify module registration and integration
- exercise audio, I/O, buffer, terminal, and console bindings
- provide simple examples for development and CI validation

## Common commands

Run the multimedia bridge demo:

```bash
./build/EngineBuilder run demo_app/bridge_multimedia.js
```

Run the general module smoke script:

```bash
./out/build/default/EngineBuilder run demo_app/modules_smoke.js
```

Run the Git module smoke test:

```bash
./out/build/default/EngineBuilder run demo_app/git_module_smoke.js
```

Run the crypto module smoke test:

```bash
./out/build/default/EngineBuilder run demo_app/crypto_module_smoke.js
```

Run the async I/O smoke test:

```bash
./out/build/default/EngineBuilder run demo_app/io_async_module_smoke.js
```

Run the buffer smoke test:

```bash
./out/build/default/EngineBuilder run demo_app/buffer_smoke.js
```

Run the console smoke test:

```bash
./out/build/default/EngineBuilder run demo_app/console_smoke.js
```

Run the terminal snapshot smoke test:

```bash
./out/build/default/EngineBuilder run demo_app/terminal_snapshot_smoke.js
```

Run the standalone terminal manager smoke test:

```bash
./out/build/default/FluxTerminalManagerSmoke
```

Run the standalone ANSI renderer smoke test:

```bash
./out/build/default/FluxTerminalAnsiSmoke
```

## Smoke test coverage

- OpenCV
- CUDA / cuDNN
- Skia
- FFmpeg
- ANGLE
- `Buffer` APIs
- console runtime functionality
- terminal snapshot and rendering behavior
- async I/O and registry integration

## Notes

These scripts are intended for validation and experimentation. They help confirm that the runtime environment, native bridges, and JavaScript-facing modules are operating as expected during refactoring and daily development.
