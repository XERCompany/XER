/**
 * This file is part of XER, Network open source project.
 * Copyright (C) 2026 Yoshi A.
 */
#include "content/network/transport/transport_io_buffer.h"

namespace XER::network::transport {

IOBuffer::IOBuffer(size_t size) {
    holder_ = XER::AsyncIO::IO::Async::IOBufferPool::Instance().Acquire(size);
    holder_->resize(size);
    data_ = holder_->data();
    size_ = size;
}

IOBuffer::~IOBuffer() = default;

}  // namespace XER::network::transport
