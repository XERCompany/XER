#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace XER::Cache {
namespace detail {
struct SmcRing;
struct SmcSharedLayout;
}  // namespace detail

// Bidirectional named byte channel for fast inter-process data
// exchange. Two shared-memory rings (A->B and B->A) carry
// length-prefixed messages; named events let a peer block efficiently
// waiting for the next message.
//
// Roles are symmetric: whichever side creates the channel owns the
// shared objects; both sides send through Send() and receive through
// Recv()/TryRecv(). Single producer / single consumer per direction —
// i.e. exactly two peers per channel.
//
//   Host  : SharedMemoryChannel::Create("ui-render", 1<<20)
//   Peer  : SharedMemoryChannel::Open("ui-render")
class SharedMemoryChannel {
public:
    ~SharedMemoryChannel();
    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;

    static std::unique_ptr<SharedMemoryChannel> Create(
        const std::string& name, uint32_t capacity_bytes,
        std::string* error_out = nullptr);
    static std::unique_ptr<SharedMemoryChannel> Open(
        const std::string& name, std::string* error_out = nullptr);

    // Length-prefixed message into the outbound ring. Blocks up to
    // timeout_ms waiting for space; 0 = non-blocking. Returns false on
    // timeout or when the channel is closed. Messages larger than
    // capacity-4 are rejected outright.
    bool Send(const void* data, uint32_t bytes, uint32_t timeout_ms = 100);
    bool Send(const std::vector<uint8_t>& data, uint32_t timeout_ms = 100) {
        return Send(data.data(), static_cast<uint32_t>(data.size()),
                    timeout_ms);
    }

    // Next inbound message. TryRecv returns false when empty; Recv
    // blocks up to timeout_ms. Zero-length messages are legal.
    bool TryRecv(std::vector<uint8_t>* out);
    bool Recv(std::vector<uint8_t>* out, uint32_t timeout_ms = 100);

    // Bytes buffered inbound right now.
    uint32_t InboundAvailable() const;

    // Wake all waiters; peers see IsClosed and bail out of blocking
    // calls. Called automatically by the creator's destructor.
    void SignalClose();
    bool IsClosed() const;

    const std::string& name() const { return name_; }
    bool is_host() const { return host_; }

private:
    SharedMemoryChannel() = default;

    std::string name_;
    bool host_ = false;
    void* mapping_ = nullptr;  // HANDLE
    detail::SmcSharedLayout* shared_ = nullptr;
    detail::SmcRing* out_ring_ = nullptr;  // direction we write
    detail::SmcRing* in_ring_ = nullptr;   // direction we read
    void* out_event_ = nullptr;   // HANDLE: in-ring data available
    void* in_event_ = nullptr;    // HANDLE
    void* out_space_ = nullptr;   // HANDLE: out-ring space available
    void* in_space_ = nullptr;    // HANDLE
};

}  // namespace XER::Cache
