# Lunca - High-Performance Audio Streaming

Lunca is a high-performance audio streaming system built with deep module architecture. It provides real-time audio streaming over UDP with minimal latency.

## Overview

Lunca enables real-time audio streaming without standard protocols like HTTP or WebSocket. It uses UDP for maximum speed and efficiency with application-level reliability mechanisms.

## Key Features

- **Low Latency**: Optimized for minimal delay audio streaming
- **UDP-Based**: Uses UDP for maximum speed and efficiency
- **Custom URI Protocol**: Supports `lunca://<IP>/on/<STREAM_ID>` for stream control
- **Cross-Platform**: Designed for Windows, Linux, macOS
- **Deep Module Architecture**: Small interfaces with maximum hidden complexity
- **XER Integration**: Uses XER audio subsystem for device abstraction
- **Opus Codec**: High-efficiency audio compression
- **Modular Design**: Clear separation into common, streaming, and protocol modules

## Architecture

```
src/lunca/
├── common/                  # Shared utilities (interface layer)
│   ├── packet.h/cc          # Network packet structures
│   └── udp_socket.h/cc      # UDP socket abstraction
├── streaming/               # Audio streaming logic (TODO)
│   ├── audio_capture.h/cc   # Audio capture from XER
│   ├── audio_encoder.h/cc   # Audio encoding (Opus)
│   ├── audio_decoder.h/cc   # Audio decoding (Opus)
│   ├── audio_playback.h/cc  # Audio playback via XER
│   └── stream_manager.h/cc  # Stream lifecycle management
├── protocol/                # Protocol handling (TODO)
│   ├── control_message.h/cc # Control message parsing
│   ├── uri_parser.h/cc      # URI protocol parsing
│   └── protocol_handler.h/cc # Protocol state machine
├── server/                  # Server implementation (TODO)
│   ├── lunca_server.h/cc    # Main server class
│   └── client_connection.h/cc # Client connection management
├── client/                  # Client implementation (TODO)
│   ├── lunca_client.h/cc    # Main client class
│   └── stream_receiver.h/cc # Stream reception
├── ARCHITECTURE.md          # Detailed architecture documentation
└── README.md
```

## Design Principles

### Deep Module Strategy
- **Small Interface**: Minimal public API (e.g., UDPSocket has only 4 core methods)
- **Deep Implementation**: Complex logic hidden in private implementation (PIMPL pattern)
- **High Leverage**: One interface call enables complex operations
- **Local Changes**: Modifications contained within modules
- **Testability**: Clean seams for mocking and testing

### Module Separation
- **common/**: Shared utilities used by both client and server
- **streaming/**: Audio capture, encoding, decoding, playback logic
- **protocol/**: Control message parsing and URI handling
- **server/**: Server-specific implementation
- **client/**: Client-specific implementation

## Protocol Specification

### URI Format
```
lunca://<SERVER_IP>/<COMMAND>/<STREAM_ID>
```

Examples:
- `lunca://192.168.1.100/on/mic0` - Start stream
- `lunca://192.168.1.100/off/mic0` - Stop stream
- `lunca://192.168.1.100/status/mic0` - Query status

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
[Header (8 bytes)][Payload]
Header:
  - Stream ID: 4 bytes
  - Sequence Number: 2 bytes
  - Timestamp: 2 bytes
Payload: Encoded audio data (Opus)
```

## Technology Stack

- **Language**: C++17 or newer
- **Network**: Raw sockets (WinAPI on Windows, POSIX on Linux/macOS)
- **Audio**: XER audio subsystem for I/O, Opus for encoding/decoding
- **Build**: CMake

## Usage Examples

### UDP Socket

```cpp
#include "lunca/common/udp_socket.h"

using namespace Lunca::Common;

// Create socket
auto socket = UDPSocket::Create();

// Bind to address
socket->Bind("0.0.0.0", 12345);

// Send data
std::vector<uint8_t> data = {0x01, 0x02, 0x03};
socket->SendTo(data, "192.168.1.100", 12345);

// Receive data
std::vector<uint8_t> received;
std::string from_addr;
int from_port;
socket->Receive(received, &from_addr, &from_port);
```

### Audio Packet

```cpp
#include "lunca/common/packet.h"

using namespace Lunca::Common;

// Create packet
AudioPacketHeader header(1, 100, 5000);  // stream_id=1, seq=100, ts=5000ms
AudioPacket packet(header);

// Set payload
std::vector<uint8_t> encoded_audio = {/* Opus encoded data */};
packet.SetPayload(encoded_audio);

// Serialize
std::vector<uint8_t> serialized = packet.Serialize();

// Deserialize
AudioPacket received_packet;
received_packet.Deserialize(serialized);
```

## Implementation Status

### Completed
- ✅ Deep module architecture design
- ✅ Common module (packet structures)
- ✅ UDP socket abstraction (Windows/Linux support)
- ✅ Architecture documentation

### TODO
- ⏳ Streaming module (capture, encoder, decoder, playback)
- ⏳ Protocol module (URI parser, control messages)
- ⏳ Server implementation
- ⏳ Client implementation
- ⏳ XER audio subsystem integration
- ⏳ Opus codec integration
- ⏳ Comprehensive tests
- ⏳ Usage examples

## Deep Module Benefits

### Leverage
- `UDPSocket::Bind()` handles socket creation, configuration, and binding
- `AudioPacket::Serialize()` handles header/payload serialization
- Complex operations through simple interface calls

### Locality
- Network logic isolated in common/ directory
- Audio logic in streaming/ directory
- Protocol logic in protocol/ directory
- Changes don't spread across codebase

### Testability
- UDP socket mockable for integration tests
- Packet structures testable independently
- Clean seams for dependency injection

## Building

### Prerequisites
- CMake 3.10+
- C++17 compatible compiler
- XER audio subsystem
- Opus codec library

### CMake Integration
```cmake
# Add to main CMakeLists.txt
if(WIN32)
    add_subdirectory(src/lunca)
endif()
```

## Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md) - Detailed architecture and design
- [README.md](README.md) - This file

## Development

### Adding New Features
1. Keep interface small
2. Hide complexity in implementation
3. Use PIMPL pattern for ABI stability
4. Document the interface clearly
5. Add tests for the interface

### Code Style
- Follow XER project conventions
- Use C++17 features where appropriate
- Prefer const correctness
- Use RAII for resource management

## License

Part of XER project. See main project LICENSE.
