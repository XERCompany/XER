#ifndef LUNCA_COMMON_UDP_SOCKET_H
#define LUNCA_COMMON_UDP_SOCKET_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Lunca::Common {

/**
 * UDPSocket - UDP socket abstraction
 *
 * Deep module with small interface:
 * - Bind(): Bind to address and port
 * - SendTo(): Send data to endpoint
 * - Receive(): Receive data from endpoint
 * - Close(): Close socket
 *
 * All platform-specific socket complexity hidden in implementation.
 */
class UDPSocket {
public:
    using ReceiveCallback = std::function<void(const std::vector<uint8_t>& data,
                                                const std::string& from_address,
                                                int from_port)>;

    static std::shared_ptr<UDPSocket> Create();

    // Small interface - core operations only
    bool Bind(const std::string& address, int port);
    bool SendTo(const std::vector<uint8_t>& data, const std::string& address, int port);
    bool Receive(std::vector<uint8_t>& data, std::string* from_address, int* from_port);
    void Close();

    // Async receive with callback
    void SetReceiveCallback(ReceiveCallback callback);
    void StartAsyncReceive();
    void StopAsyncReceive();

    // Status
    bool IsBound() const;
    bool IsOpen() const;
    int GetLocalPort() const;
    std::string GetLocalAddress() const;

    // Configuration
    void SetReceiveBufferSize(int size);
    void SetSendBufferSize(int size);
    void SetNonBlocking(bool non_blocking);

    // Prevent copying
    UDPSocket(const UDPSocket&) = delete;
    UDPSocket& operator=(const UDPSocket&) = delete;

    virtual ~UDPSocket();

protected:
    UDPSocket();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Lunca::Common

#endif // LUNCA_COMMON_UDP_SOCKET_H
