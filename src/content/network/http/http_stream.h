/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace XER::Network::Http {

// Abstract streaming interface over an established HTTP connection.
class HttpStream {
public:
    virtual ~HttpStream() = default;

    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;

    // Send chunked request body data.
    virtual bool SendChunk(const uint8_t* data, size_t len, std::string* error) = 0;
    // Read a response body chunk. Returns bytes read, 0 on EOF, -1 on error.
    virtual int  ReadChunk(uint8_t* buf, size_t capacity, std::string* error) = 0;
};

}  // namespace XER::Network::Http
