/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "async_io/io_buffer_pool.h"

namespace network::transport {

// Reference-counted I/O buffer backed by IOBufferPool.
class IOBuffer {
public:
    explicit IOBuffer(size_t size);
    ~IOBuffer();

    uint8_t*       data()       { return data_; }
    const uint8_t* data() const { return data_; }
    size_t         size() const { return size_; }

    // Span helpers
    uint8_t*       begin()       { return data_; }
    const uint8_t* begin() const { return data_; }
    uint8_t*       end()         { return data_ + size_; }
    const uint8_t* end()   const { return data_ + size_; }

private:
    AsyncIO::IO::Async::IOBufferPool::BufferPtr holder_;
    uint8_t* data_  = nullptr;
    size_t   size_  = 0;
};

}  // namespace network::transport
