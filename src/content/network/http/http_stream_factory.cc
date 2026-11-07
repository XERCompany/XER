/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/http/http_stream_factory.h"
#include "content/network/socket/socket_stream.h"

namespace XER::network::http {

namespace {

class SocketBackedHttpStream : public HttpStream {
public:
    explicit SocketBackedHttpStream(
        std::unique_ptr<transport::TransportClientSocket> sock)
        : sock_(std::move(sock)) {}

    bool IsOpen() const override { return sock_ != nullptr; }
    void Close() override { if (sock_) { sock_->Disconnect(); sock_.reset(); } }

    bool SendChunk(const uint8_t* data, size_t len, std::string* error) override {
        return sock_ && sock_->Write(data, len, error);
    }
    int ReadChunk(uint8_t* buf, size_t cap, std::string* error) override {
        if (!sock_) return -1;
        return sock_->Read(buf, cap, error);
    }

private:
    std::unique_ptr<transport::TransportClientSocket> sock_;
};

}  // namespace

std::unique_ptr<HttpStream>
HttpStreamFactory::CreateFromSocket(
    std::unique_ptr<transport::TransportClientSocket> socket,
    const std::string& /*host*/,
    std::string* /*error*/) {
    return std::make_unique<SocketBackedHttpStream>(std::move(socket));
}

}  // namespace XER::network::http
