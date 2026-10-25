# OpenAI Content Subsystem

Comprehensive integration with OpenAI API for XER runtime, providing:
- **Client**: OpenAI API client library (using `content/network` HTTP stack)
- **Agent**: Intelligent agents with memory and tools
- **Skills**: Extensible skill system
- **Sandbox**: Secure code execution environment
- **Image**: Image processing and generation
- **Text**: Text processing and analysis (using `range-v3`, `absl`, `re2`)
- **Audio**: Audio transcription (using `audio/` and Whisper)
- **Hardware**: GPU acceleration and resource management
- **DSP**: Audio feature extraction and preprocessing
- **Utilities**: String, JSON, validation utilities
- **Server**: HTTP API server

## Integration with XER

The subsystem deeply integrates with XER infrastructure:

### Network (content/network)
- HTTP requests via `network::http::HttpTransaction`
- URL handling via `network::url::URLRequest`
- Connection pooling and caching
- Proxy support
- SSL/TLS security

### Async I/O (async_io)
- Thread pool for async operations
- Task queue for job scheduling
- Resource monitoring
- Buffer management

### Audio (audio/)
- Audio loading and processing via `Engine::Audio::Core::AudioEngine`
- Real-time transcription pipeline
- DSP algorithms for audio preprocessing
- Format conversion

### Hardware Abstraction (async_io/hardware_abstraction)
- GPU detection and management via `GPUInfoProvider`
- CPU information via `CPUInfo`
- Memory monitoring via `MemoryMonitor`
- GPU-accelerated image processing
- Resource allocation and monitoring

### DSP Algorithms (audio/dsp_algorithms)
- FFT engine for spectral analysis
- STFT processor for time-frequency analysis
- Windowing functions
- Feature extraction (MFCC, spectral features)
- Voice activity detection
- Speaker diarization

### Libraries
- **range-v3**: Functional programming for text processing
- **absl**: String utilities (str_format, str_join, str_split, str_replace, escaping, numbers)
- **absl**: Synchronization (Mutex, Notification)
- **absl**: Containers (flat_hash_map, flat_hash_set)
- **re2**: Regular expressions for pattern matching
- **libuv**: Event loop for async operations (via network subsystem)

## Architecture

```
src/content/openai/
├── openai.h/cc              # Main subsystem interface
├── openai_types.h           # Common type definitions
├── client/
│   ├── openai_client.h/cc   # OpenAI API client
├── agent/
│   ├── agent.h/cc           # Agent system
├── skills/
│   ├── skill.h/cc           # Skills system
├── sandbox/
│   ├── sandbox.h/cc        # Code execution sandbox
├── image/
│   ├── image_processor.h/cc # Image processing
├── text/
│   ├── text_processor.h/cc  # Text processing
└── server/
    ├── openai_server.h/cc   # HTTP API server
```

## Features

### Client
- Chat completions with streaming support
- Image generation (DALL-E)
- Audio transcription (Whisper)
- Rate limiting and usage tracking
- Async and sync APIs

### Agent
- Base agent with memory and tools
- Specialized agents: Coding, Image, Research
- Memory management with relevance scoring
- Tool/function calling support
- State management

### Skills
- Web search
- Code execution
- File I/O
- Data analysis
- Extensible skill registry

### Sandbox
- Python and JavaScript execution
- Resource limits (memory, CPU, time)
- Security policies (strict, moderate, permissive)
- Malicious pattern detection
- Execution monitoring

### Image
- Image generation via OpenAI API
- Image analysis (object detection, classification)
- Image processing (resize, crop, rotate, filters)
- Format conversion (PNG, JPEG, WebP, GIF)
- Local processing fallback

### Text
- Summarization
- Translation
- Entity extraction
- Text rewriting
- Sentiment analysis
- Keyword extraction
- Batch processing

### Server
- HTTP API server
- Request routing (chat, image, audio, text, agent, skill, sandbox)
- Middleware system (auth, rate limiting, logging)
- Statistics tracking
- Agent and skill registration

## Usage

### Basic Client Usage

```cpp
#include "content/openai/openai.h"

using namespace Content::OpenAI;

// Create client
auto client = Client::ClientFactory::CreateFromEnv();

// Chat completion
std::vector<ChatMessage> messages;
messages.push_back({MessageRole::kUser, {{ContentType::kText, "Hello!"}}});

auto response = client->CreateChatCompletion(messages).get();
std::cout << response.choices[0].content[0].text << std::endl;
```

### Agent Usage

```cpp
// Create subsystem
auto subsystem = OpenAIFactory::CreateFromEnv();
subsystem->Initialize();

// Create agent
auto agent = subsystem->CreateCodingAgent();

// Process with agent
auto response = agent->Process("Write a function to calculate fibonacci").get();
std::cout << response.choices[0].content[0].text << std::endl;
```

### Skills Usage

```cpp
// Register skills
Skills::SkillRegistry::GetInstance().RegisterDefaultSkills();

// Execute skill
std::map<std::string, std::string> params;
params["code"] = "print('Hello, World!')";
params["language"] = "python";

auto skill = Skills::SkillRegistry::GetInstance().GetSkill("code_execution");
auto result = skill->Execute(params).get();
std::cout << result.output << std::endl;
```

### Sandbox Usage

```cpp
// Create sandbox
auto sandbox = Sandbox::SandboxFactory::CreatePythonSandbox();

// Set resource limits
Sandbox::ResourceLimits limits;
limits.max_memory_mb = 256;
limits.max_execution_time_seconds = 30;
sandbox->SetResourceLimits(limits);

// Execute code
Sandbox::ExecutionContext context;
context.language = "python";
context.code = "print('Hello from sandbox!')";

auto result = sandbox->ExecuteCode(context).get();
std::cout << result.stdout_output << std::endl;
```

### Image Processing

```cpp
// Generate image
ImageGenerationParams params;
params.prompt = "A beautiful sunset over mountains";
params.size_width = 1024;
params.size_height = 1024;

auto image_processor = subsystem->GetImageProcessor();
auto response = image_processor->GenerateImage(params).get();
std::cout << "Image URL: " << response.url << std::endl;
```

### Audio Transcription

```cpp
// Transcribe audio file
auto audio_transcriber = subsystem->GetAudioTranscriber();

Audio::TranscriptionOptions options;
options.language = "en";
options.enable_vad = true;

auto result = audio_transcriber->TranscribeFile("audio.wav", options).get();
std::cout << "Transcription: " << result.full_text << std::endl;
std::cout << "Language: " << result.language << std::endl;
std::cout << "Duration: " << result.duration << "s" << std::endl;

// Real-time transcription
audio_transcriber->SetRealTimeCallback([](const Audio::TranscriptionSegment& segment) {
    std::cout << "[" << segment.start_time << "s] " << segment.text << std::endl;
});

audio_transcriber->StartRealTimeTranscription();
// Feed audio chunks...
audio_transcriber->StopRealTimeTranscription();
```

### Text Processing

```cpp
// Summarize text
auto text_processor = subsystem->GetTextProcessor();
auto summary = text_processor->Summarize("Long text here...", 100).get();
std::cout << "Summary: " << summary << std::endl;

// Analyze sentiment
auto sentiment = text_processor->AnalyzeSentiment("I love this!").get();
std::cout << "Sentiment: " << sentiment.second << " (" << sentiment.first << ")" << std::endl;
```

### Server Usage

```cpp
// Create server with subsystem
auto subsystem = OpenAIFactory::CreateWithServer("your-api-key", 8080);
subsystem->Initialize();

// Server is now running on port 8080
// Handle requests via HandleRequest()
```

## Integration with XER

The subsystem deeply integrates with XER infrastructure:

### Network (content/network)
- HTTP requests via `network::http::HttpTransaction`
- URL handling via `network::url::URLRequest`
- Connection pooling and caching
- Proxy support
- SSL/TLS security

### Async I/O (async_io)
- Thread pool for async operations
- Task queue for job scheduling
- Resource monitoring
- Buffer management

### Audio (audio/)
- Audio loading and processing via `Engine::Audio::Core::AudioEngine`
- Real-time transcription pipeline
- DSP algorithms for audio preprocessing
- Format conversion

### Libraries
- **range-v3**: Functional programming for text processing
- **absl**: String utilities (str_format, str_join, str_split, str_replace)
- **re2**: Regular expressions for pattern matching
- **libuv**: Event loop for async operations (via network subsystem)

## Configuration

### Environment Variables
- `OPENAI_API_KEY`: OpenAI API key
- `OPENAI_ORGANIZATION`: OpenAI organization ID
- `OPENAI_BASE_URL`: Custom API base URL

### Client Configuration
```cpp
Client::OpenAIClient::Config config;
config.api_key = "your-key";
config.base_url = "https://api.openai.com/v1";
config.timeout_seconds = 60;
config.max_retries = 3;
```

### Agent Configuration
```cpp
Agent::AgentConfig config;
config.name = "MyAgent";
config.system_prompt = "You are a helpful assistant";
config.model = "gpt-4";
config.temperature = 0.7f;
config.max_tokens = 2048;
config.enable_memory = true;
config.enable_tools = true;
```

### Sandbox Configuration
```cpp
Sandbox::ResourceLimits limits;
limits.max_memory_mb = 512;
limits.max_cpu_time_seconds = 30;
limits.allow_network = false;
limits.allow_filesystem = false;
```

## Security

- Input validation and sanitization
- Malicious pattern detection in sandbox
- Rate limiting support
- Authentication middleware
- Resource limits enforcement
- Security policies (strict, moderate, permissive)

## Performance

- Async operations using thread pool
- Connection pooling (TODO)
- Caching support (TODO)
- Batch processing for text operations
- Memory-efficient streaming responses

## Future Enhancements

- Connection pooling
- Response caching
- Persistent memory storage
- Webhook support
- More sandbox languages
- Advanced image processing
- Real-time audio processing
- Multi-agent collaboration
- Agent federation

## Dependencies

### XER Components
- `content/network` - HTTP client, URL handling, SSL/TLS
- `async_io` - Thread pool, async operations, buffer management
- `audio/` - Audio engine, DSP algorithms, format conversion

### External Libraries
- **range-v3** - Functional programming utilities
- **absl** - Google Abseil (strings, synchronization, containers)
- **re2** - Regular expressions (fast C++ regex library)
- **libuv** - Event loop (via network subsystem)

### Build Dependencies
- CMake 3.21+
- C++23 compiler
- OpenSSL (for network SSL/TLS)
- FFmpeg (for audio processing, optional)

## License

Part of XER project. See main project LICENSE.
