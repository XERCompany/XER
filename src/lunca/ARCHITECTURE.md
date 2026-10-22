# Lunca Architecture

## Overview

Lunca is a high-performance audio streaming system built with deep module architecture principles. It provides real-time audio streaming over UDP with minimal latency.

## Design Principles

### Deep Module Strategy
- **Small Interface**: Minimal public API with maximum hidden complexity
- **High Leverage**: Complex operations through simple function calls
- **Local Changes**: Modifications contained within modules
- **Testability**: Clean seams for mocking and testing

## Architecture

```
src/lunca/
├── common/                  # Shared utilities (interface layer)
│   ├── audio_buffer.h/cc    # Audio buffer management
│   ├── codec_interface.h    # Codec abstraction
│   ├── udp_socket.h/cc      # UDP socket abstraction
│   └── packet.h/cc          # Network packet structures
├── streaming/               # Audio streaming logic
│   ├── audio_capture.h/cc   # Audio capture from XER
│   ├── audio_encoder.h/cc   # Audio encoding (Opus)
│   ├── audio_decoder.h/cc   # Audio decoding (Opus)
│   ├── audio_playback.h/cc  # Audio playback via XER
│   └── stream_manager.h/cc  # Stream lifecycle management
├── protocol/                # Protocol handling
│   ├── control_message.h/cc # Control message parsing
│   ├── uri_parser.h/cc      # URI protocol parsing
│   └── protocol_handler.h/cc # Protocol state machine
├── server/                  # Server implementation
│   ├── lunca_server.h/cc    # Main server class
│   └── client_connection.h/cc # Client connection management
├── client/                  # Client implementation
│   ├── lunca_client.h/cc    # Main client class
│   └── stream_receiver.h/cc # Stream reception
└── README.md
```

## Core Interfaces

### Audio Capture (Deep Module)

```cpp
namespace Lunca::Streaming {

class AudioCapture {
public:
    // Small interface
    static std::shared_ptr<AudioCapture> Create();

    bool Initialize(const std::string& device_id);
    void Start();
    void Stop();
    bool IsCapturing() const;

    // Callback for audio data
    using AudioCallback = std::function<void(const float* samples, int frame_count)>;
    void SetAudioCallback(AudioCallback callback);

    // Configuration
    void SetSampleRate(int sample_rate);
    void SetChannels(int channels);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Streaming
```

**Deep Implementation Hides**:
- XER audio device integration
- Buffer management
- Thread synchronization
- Platform-specific audio APIs

### Audio Encoder (Deep Module)

```cpp
namespace Lunca::Streaming {

class AudioEncoder {
public:
    // Small interface
    static std::shared_ptr<AudioEncoder> Create();

    bool Initialize(int sample_rate, int channels, int bitrate);
    bool Encode(const float* input, std::vector<uint8_t>& output);
    void Reset();

    // Configuration
    void SetBitrate(int bitrate);
    void SetComplexity(int complexity);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Streaming
```

**Deep Implementation Hides**:
- Opus codec initialization
- Frame encoding logic
- Bitrate adaptation
- Error handling

### UDP Socket (Deep Module)

```cpp
namespace Lunca::Common {

class UDPSocket {
public:
    // Small interface
    static std::shared_ptr<UDPSocket> Create();

    bool Bind(const std::string& address, int port);
    bool SendTo(const std::vector<uint8_t>& data, const std::string& address, int port);
    bool Receive(std::vector<uint8_t>& data, std::string* from_address, int* from_port);
    void Close();

    bool IsBound() const;
    int GetLocalPort() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Common
```

**Deep Implementation Hides**:
- Socket creation and configuration
- Platform-specific socket APIs
- Error handling
- Buffer management

### Protocol Handler (Deep Module)

```cpp
namespace Lunca::Protocol {

class ProtocolHandler {
public:
    // Small interface
    static std::shared_ptr<ProtocolHandler> Create();

    bool ParseURI(const std::string& uri, ControlMessage& message);
    std::string SerializeMessage(const ControlMessage& message);
    bool HandleMessage(const ControlMessage& message);

    // Commands
    void SetOnStart(std::function<void(const std::string& stream_id)> callback);
    void SetOnStop(std::function<void(const std::string& stream_id)> callback);
    void SetOnStatus(std::function<void(const StreamStatus& status)> callback);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Protocol
```

**Deep Implementation Hides**:
- URI parsing logic
- Message serialization/deserialization
- State machine for protocol
- Error handling

## Server Architecture

```cpp
namespace Lunca::Server {

class LuncaServer {
public:
    // Small interface
    static std::shared_ptr<LuncaServer> Create();

    bool Initialize(const std::string& bind_address, int control_port, int audio_port);
    void Start();
    void Stop();
    bool IsRunning() const;

    // Stream management
    bool StartStream(const std::string& stream_id, const std::string& client_address, int client_port);
    bool StopStream(const std::string& stream_id);

    // Configuration
    void SetDefaultSampleRate(int sample_rate);
    void SetDefaultBitrate(int bitrate);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Server
```

**Deep Implementation Hides**:
- Control message listener
- Audio stream broadcasting
- Client connection management
- Stream lifecycle
- XER integration

## Client Architecture

```cpp
namespace Lunca::Client {

class LuncaClient {
public:
    // Small interface
    static std::shared_ptr<LuncaClient> Create();

    bool Initialize(const std::string& bind_address, int audio_port);
    void Start();
    void Stop();
    bool IsRunning() const;

    // Stream control
    bool ConnectToStream(const std::string& server_address, int control_port, const std::string& stream_id);
    bool Disconnect();

    // Status
    StreamStatus GetStatus() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Client
```

**Deep Implementation Hides**:
- Audio stream reception
- Decoding and playback
- Control message sending
- Stream state management
- XER integration

## Data Flow

### Server Flow
```
Audio Source (XER)
    ↓
AudioCapture
    ↓
AudioEncoder (Opus)
    ↓
UDPSocket (Send)
    ↓
Network
```

### Client Flow
```
Network
    ↓
UDPSocket (Receive)
    ↓
AudioDecoder (Opus)
    ↓
AudioPlayback (XER)
    ↓
Audio Output
```

### Control Flow
```
URI: lunca://192.168.1.100/on/mic0
    ↓
URI Parser
    ↓
Control Message (JSON)
    ↓
UDP Socket (Send)
    ↓
Server Handler
    ↓
Stream Start/Stop
```

## Protocol Specification

### URI Format
```
lunca://<SERVER_IP>/<COMMAND>/<STREAM_ID>
```

Example: `lunca://192.168.1.100/on/mic0`

### Control Message (JSON)
```json
{
    "command": "ON|OFF|STATUS",
    "stream_id": "mic0",
    "client_ip_for_audio": "192.168.1.101",
    "client_port_for_audio": "12346",
    "sample_rate": 48000,
    "channels": 2,
    "bitrate": 128000
}
```

### Audio Packet Format
```
[Header][Payload]
Header: 8 bytes
  - Stream ID: 4 bytes
  - Sequence Number: 2 bytes
  - Timestamp: 2 bytes
Payload: Encoded audio data (Opus)
```

## Deep Module Benefits

### Leverage
- `AudioCapture::Start()` handles entire capture lifecycle
- `AudioEncoder::Encode()` handles complex encoding logic
- `ProtocolHandler::ParseURI()` handles URI parsing and message creation

### Locality
- XER integration isolated in streaming/ directory
- Network logic in common/ directory
- Protocol handling in protocol/ directory
- Changes don't spread across codebase

### Testability
- Codec interface mockable for testing
- UDP socket mockable for integration tests
- Audio capture testable without real devices
- Clean seams for dependency injection

## Implementation Status

### Planned
- ⏳ Common module (UDP socket, packet structures)
- ⏳ Streaming module (capture, encoder, decoder, playback)
- ⏳ Protocol module (URI parser, control messages)
- ⏳ Server implementation
- ⏳ Client implementation
- ⏳ Integration with XER audio subsystem
- ⏳ Opus codec integration
- ⏳ Comprehensive tests
- ⏳ Documentation

## Next Steps

1. Implement common utilities (UDP socket, packet structures)
2. Create codec interface and Opus implementation
3. Implement audio capture/playback with XER
4. Create protocol handler with URI parsing
5. Implement server with stream management
6. Implement client with stream reception
7. Add comprehensive tests
8. Write usage examples
