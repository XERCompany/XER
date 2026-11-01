# OpenAI integration

The `src/content/openai` directory provides a compact integration layer for OpenAI-style APIs and related LLM workflows.

## Scope

- OpenAI client access
- chat and prompt-based interactions
- agent and skill abstractions
- text, image, and audio processing helpers
- server-style request routing concepts
- integration with other XER subsystems such as network, async I/O, and audio

## Typical architecture

- `client/` — API transport and request creation
- `agent/` — agent abstractions and orchestration logic
- `skills/` — reusable capabilities or tool-like functions
- `sandbox/` — isolated execution environment for code or workflows
- `image/` — image generation or processing
- `text/` — text analysis, summarization, and generation helpers
- `server/` — server-facing request routing

## Relationship to the rest of the project

This subsystem is designed to fit into the larger XER runtime rather than exist as a standalone product. It can use the project’s networking and async primitives and is expected to coexist with the other content and media subsystems.

## Example

```cpp
#include "content/openai/openai.h"

using namespace Content::OpenAI;

auto client = Client::ClientFactory::CreateFromEnv();
auto response = client->CreateChatCompletion(messages).get();
```

## Notes

The exact features available in this module depend on the chosen build configuration and dependency set. This README is intended as a high-level overview of the integration points and responsibilities rather than a strict specification of every runtime capability.
