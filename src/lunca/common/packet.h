#ifndef LUNCA_COMMON_PACKET_H
#define LUNCA_COMMON_PACKET_H

#include <cstdint>
#include <string>
#include <vector>

namespace Lunca::Common {

/**
 * AudioPacketHeader - Header for audio packets
 *
 * Deep module with small interface - just data structure
 * All serialization/deserialization logic in implementation
 */
struct AudioPacketHeader {
    uint32_t stream_id;      // Stream identifier
    uint16_t sequence;       // Sequence number
    uint16_t timestamp;      // Timestamp in milliseconds

    AudioPacketHeader()
        : stream_id(0)
        , sequence(0)
        , timestamp(0) {}

    AudioPacketHeader(uint32_t id, uint16_t seq, uint16_t ts)
        : stream_id(id)
        , sequence(seq)
        , timestamp(ts) {}
};

/**
 * AudioPacket - Complete audio packet
 */
class AudioPacket {
public:
    AudioPacket();
    explicit AudioPacket(const AudioPacketHeader& header);

    // Small interface
    void SetHeader(const AudioPacketHeader& header);
    const AudioPacketHeader& GetHeader() const;

    void SetPayload(const std::vector<uint8_t>& payload);
    const std::vector<uint8_t>& GetPayload() const;

    // Serialization
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);

    // Utility
    size_t GetTotalSize() const;
    bool IsValid() const;

private:
    static constexpr size_t HEADER_SIZE = sizeof(AudioPacketHeader);

    AudioPacketHeader header_;
    std::vector<uint8_t> payload_;
};

/**
 * ControlMessage - Control protocol message
 */
enum class ControlCommand {
    ON,         // Start streaming
    OFF,        // Stop streaming
    STATUS,     // Query status
    ERROR       // Error response
};

struct ControlMessage {
    ControlCommand command;
    std::string stream_id;
    std::string client_ip_for_audio;
    int client_port_for_audio;
    int sample_rate;
    int channels;
    int bitrate;
    std::string error_message;

    ControlMessage()
        : command(ControlCommand::STATUS)
        , client_port_for_audio(0)
        , sample_rate(48000)
        , channels(2)
        , bitrate(128000) {}
};

/**
 * StreamStatus - Status of a stream
 */
struct StreamStatus {
    std::string stream_id;
    bool is_active;
    int sample_rate;
    int channels;
    int bitrate;
    uint64_t packets_sent;
    uint64_t packets_received;
    double latency_ms;

    StreamStatus()
        : is_active(false)
        , sample_rate(48000)
        , channels(2)
        , bitrate(128000)
        , packets_sent(0)
        , packets_received(0)
        , latency_ms(0.0) {}
};

} // namespace Lunca::Common

#endif // LUNCA_COMMON_PACKET_H
