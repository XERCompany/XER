#include "udp_socket.h"
#include <thread>
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
typedef int SOCKET;
#endif

namespace Lunca::Common {

// Windows WSA initialization (Windows only)
#ifdef _WIN32
class WSAInitializer {
public:
    WSAInitializer() {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            throw std::runtime_error("Failed to initialize Winsock");
        }
    }

    ~WSAInitializer() {
        WSACleanup();
    }

    static bool IsInitialized() {
        static WSAInitializer instance;
        return true;
    }
};
#endif

// Private implementation
class UDPSocket::Impl {
public:
    Impl()
        : socket_(INVALID_SOCKET)
        , bound_(false)
        , open_(false)
        , local_port_(0)
        , receive_buffer_size_(65536)
        , send_buffer_size_(65536)
        , non_blocking_(false)
        , async_running_(false) {

#ifdef _WIN32
        WSAInitializer::IsInitialized();
#endif
    }

    ~Impl() {
        Close();
    }

    bool Bind(const std::string& address, int port) {
        if (bound_) {
            return false;
        }

        // Create socket
        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ == INVALID_SOCKET) {
            return false;
        }

        // Set socket options
        int opt = 1;
        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

        // Bind to address
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (address.empty() || address == "0.0.0.0" || address == "*") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
        }

        if (bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
#ifdef _WIN32
            closesocket(socket_);
#else
            close(socket_);
#endif
            socket_ = INVALID_SOCKET;
            return false;
        }

        bound_ = true;
        open_ = true;
        local_port_ = port;
        local_address_ = address.empty() ? "0.0.0.0" : address;

        return true;
    }

    bool SendTo(const std::vector<uint8_t>& data, const std::string& address, int port) {
        if (!open_ || socket_ == INVALID_SOCKET) {
            return false;
        }

        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, address.c_str(), &addr.sin_addr);

        int sent = sendto(socket_,
                         reinterpret_cast<const char*>(data.data()),
                         static_cast<int>(data.size()),
                         0,
                         reinterpret_cast<sockaddr*>(&addr),
                         sizeof(addr));

        return sent == static_cast<int>(data.size());
    }

    bool Receive(std::vector<uint8_t>& data, std::string* from_address, int* from_port) {
        if (!open_ || socket_ == INVALID_SOCKET) {
            return false;
        }

        data.resize(receive_buffer_size_);
        sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int received = recvfrom(socket_,
                                reinterpret_cast<char*>(data.data()),
                                static_cast<int>(data.size()),
                                0,
                                reinterpret_cast<sockaddr*>(&from_addr),
                                &from_len);

        if (received == SOCKET_ERROR) {
            return false;
        }

        data.resize(received);

        if (from_address) {
            char addr_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, addr_str, INET_ADDRSTRLEN);
            *from_address = addr_str;
        }

        if (from_port) {
            *from_port = ntohs(from_addr.sin_port);
        }

        return true;
    }

    void Close() {
        if (socket_ != INVALID_SOCKET) {
            StopAsyncReceive();

#ifdef _WIN32
            closesocket(socket_);
#else
            close(socket_);
#endif
            socket_ = INVALID_SOCKET;
        }

        bound_ = false;
        open_ = false;
        local_port_ = 0;
    }

    void SetReceiveCallback(ReceiveCallback callback) {
        receive_callback_ = callback;
    }

    void StartAsyncReceive() {
        if (async_running_ || !receive_callback_) {
            return;
        }

        async_running_ = true;
        receive_thread_ = std::thread([this]() {
            while (async_running_) {
                std::vector<uint8_t> data;
                std::string from_address;
                int from_port;

                if (Receive(data, &from_address, &from_port)) {
                    receive_callback_(data, from_address, from_port);
                }

                // Small sleep to prevent CPU spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    void StopAsyncReceive() {
        async_running_ = false;
        if (receive_thread_.joinable()) {
            receive_thread_.join();
        }
    }

    bool IsBound() const {
        return bound_;
    }

    bool IsOpen() const {
        return open_;
    }

    int GetLocalPort() const {
        return local_port_;
    }

    std::string GetLocalAddress() const {
        return local_address_;
    }

    void SetReceiveBufferSize(int size) {
        receive_buffer_size_ = size;
        if (socket_ != INVALID_SOCKET) {
            setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
                       reinterpret_cast<const char*>(&size), sizeof(size));
        }
    }

    void SetSendBufferSize(int size) {
        send_buffer_size_ = size;
        if (socket_ != INVALID_SOCKET) {
            setsockopt(socket_, SOL_SOCKET, SO_SNDBUF,
                       reinterpret_cast<const char*>(&size), sizeof(size));
        }
    }

    void SetNonBlocking(bool non_blocking) {
        non_blocking_ = non_blocking;
#ifdef _WIN32
        u_long mode = non_blocking ? 1 : 0;
        ioctlsocket(socket_, FIONBIO, &mode);
#else
        int flags = fcntl(socket_, F_GETFL, 0);
        fcntl(socket_, F_SETFL, non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
#endif
    }

private:
    SOCKET socket_;
    bool bound_;
    bool open_;
    int local_port_;
    std::string local_address_;
    int receive_buffer_size_;
    int send_buffer_size_;
    bool non_blocking_;

    ReceiveCallback receive_callback_;
    bool async_running_;
    std::thread receive_thread_;
};

// UDPSocket implementation
UDPSocket::UDPSocket() : impl_(std::make_unique<Impl>()) {}

UDPSocket::~UDPSocket() = default;

std::shared_ptr<UDPSocket> UDPSocket::Create() {
    return std::shared_ptr<UDPSocket>(new UDPSocket());
}

bool UDPSocket::Bind(const std::string& address, int port) {
    return impl_->Bind(address, port);
}

bool UDPSocket::SendTo(const std::vector<uint8_t>& data, const std::string& address, int port) {
    return impl_->SendTo(data, address, port);
}

bool UDPSocket::Receive(std::vector<uint8_t>& data, std::string* from_address, int* from_port) {
    return impl_->Receive(data, from_address, from_port);
}

void UDPSocket::Close() {
    impl_->Close();
}

void UDPSocket::SetReceiveCallback(ReceiveCallback callback) {
    impl_->SetReceiveCallback(callback);
}

void UDPSocket::StartAsyncReceive() {
    impl_->StartAsyncReceive();
}

void UDPSocket::StopAsyncReceive() {
    impl_->StopAsyncReceive();
}

bool UDPSocket::IsBound() const {
    return impl_->IsBound();
}

bool UDPSocket::IsOpen() const {
    return impl_->IsOpen();
}

int UDPSocket::GetLocalPort() const {
    return impl_->GetLocalPort();
}

std::string UDPSocket::GetLocalAddress() const {
    return impl_->GetLocalAddress();
}

void UDPSocket::SetReceiveBufferSize(int size) {
    impl_->SetReceiveBufferSize(size);
}

void UDPSocket::SetSendBufferSize(int size) {
    impl_->SetSendBufferSize(size);
}

void UDPSocket::SetNonBlocking(bool non_blocking) {
    impl_->SetNonBlocking(non_blocking);
}

} // namespace Lunca::Common
