#include "xer/xer_buffer.h"

#include "helper/buffer.h"

#include <fstream>

namespace {

XER::Helper::Buffer::ConstSpan MakeConstSpan(const std::vector<std::uint8_t>& bytes) {
    return XER::Helper::Buffer::ConstSpan(bytes.data(), bytes.size());
}

XER::Helper::Buffer::ConstSpan MakeConstSpan(const std::uint8_t* data, std::size_t size) {
    return XER::Helper::Buffer::ConstSpan(data, size);
}

}  // namespace

namespace Xer {

XerBuffer::XerBuffer(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

XerBuffer XerBuffer::FromString(std::string_view text) {
    return XerBuffer(XER::Helper::Buffer::FromString(text));
}

XerBuffer XerBuffer::FromFile(const std::filesystem::path& path, std::string* error_out) {
    if (!std::filesystem::exists(path)) {
        if (error_out != nullptr) {
            *error_out = "buffer source file does not exist";
        }
        return XerBuffer();
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error_out != nullptr) {
            *error_out = "failed to open buffer source file";
        }
        return XerBuffer();
    }

    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);

    std::vector<std::uint8_t> bytes;
    if (size > 0) {
        bytes.resize(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()), size);
    }

    if (!input && size > 0) {
        if (error_out != nullptr) {
            *error_out = "failed to read buffer source file";
        }
        return XerBuffer();
    }

    if (error_out != nullptr) {
        error_out->clear();
    }
    return XerBuffer(std::move(bytes));
}

void XerBuffer::Append(std::string_view text) {
    XER::Helper::Buffer::AppendString(&bytes_, text);
}

void XerBuffer::Append(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }
    XER::Helper::Buffer::AppendBytes(&bytes_, MakeConstSpan(data, size));
}

std::string XerBuffer::ToString() const {
    return XER::Helper::Buffer::ToString(MakeConstSpan(bytes_));
}

}  // namespace Xer