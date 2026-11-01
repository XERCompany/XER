# AI subsystem

The `src/content/ai` directory contains the project’s model-building and ML-oriented infrastructure. It is structured around a broad set of runtime capabilities, including model construction, inference, and import support for common frameworks.

## Scope

- model building and training scaffolding
- tensor and data pipeline utilities
- import support for common model formats such as ONNX and OpenVINO-like flows
- inference and optimization layers
- generative AI-related foundations such as diffusion pipelines
- runtime capability checks and backend-aware execution decisions

## High-level organization

- `models_builder/` — model construction, training, serialization, and optimization
- `ml/` — ML utilities, tensor abstractions, and data processing helpers
- `reader/` — model format adapters and validation layers
- `sd_base/`, `img2img/`, `txt2img/` — diffusion and generation-related foundations

## Design notes

This subsystem is modular and optional. It can be enabled depending on platform capabilities and dependency availability. Some components are best-effort integrations that leverage CUDA, OpenCV, Eigen, XNNPACK, or similar tooling when available.

## Example

```cpp
using namespace Engine::ModelsBuilder;

auto model = std::make_unique<Core::Model>("MyModel");
model->AddLayer(std::make_unique<Core::DenseLayer>(128, 64));
model->Build({1, 64});
model->Compile();
```

## Status

The AI subsystem contains a substantial amount of infrastructure and is evolving. Some areas are already implemented and reusable; others remain experimental or incomplete depending on the current build configuration.
