/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::Network::Socket {

// Buffered stream over a raw socket fd.
class SocketStream {
public:
    explicit SocketStream(int fd);
    ~SocketStream();

    // Reads exactly |n| bytes into |buf|. Returns false on error/EOF.
    bool ReadExact(std::vector<uint8_t>* buf, size_t n, std::string* error);

    // Reads until |delimiter| is found (e.g. "\r\n"). Returns line (incl. delim).
    bool ReadLine(std::string* line, std::string* error);

    // Writes all |data|. Returns false on error.
    bool WriteAll(const uint8_t* data, size_t len, std::string* error);
    bool WriteAll(const std::string& data, std::string* error);

    int fd() const { return fd_; }

private:
    int                  fd_    = -1;
    std::vector<uint8_t> rbuf_;   // read-ahead buffer
};

}  // namespace XER::Network::Socket
