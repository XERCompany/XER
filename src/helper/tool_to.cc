#include "tool_to.h"

#include "async_io/async_file_reader.h"
#include "async_io/async_file_writer.h"

#include <algorithm>
#include <cstdint>

namespace Engine::Helper {

bool ToolTo::CopyBinary(const std::filesystem::path& source,
                        const std::filesystem::path& destination,
                        std::string* error_message) {
    AsyncIO::IO::Async::AsyncFileReader reader;
    if (!reader.OpenFile(source.string())) {
        if (error_message != nullptr) {
            *error_message = "Failed to open source binary: " + source.string();
        }
        return false;
    }

    AsyncIO::IO::Async::AsyncFileWriter writer;
    if (!writer.CreateFile(destination.string(), false)) {
        if (error_message != nullptr) {
            *error_message = "Failed to open destination binary: " + destination.string();
        }
        return false;
    }

    constexpr size_t kChunkSize = 256 * 1024;
    const int64_t file_size = reader.GetFileSize();
    if (file_size < 0) {
        if (error_message != nullptr) {
            *error_message = "Failed to query source binary size: " + source.string();
        }
        return false;
    }

    size_t remaining = static_cast<size_t>(file_size);
    while (remaining > 0) {
        const size_t read_size = std::min(kChunkSize, remaining);

        std::vector<uint8_t> chunk;
        if (!reader.ReadSync(read_size, chunk)) {
            if (error_message != nullptr) {
                *error_message = "Failed while reading source binary: " + source.string();
            }
            return false;
        }

        if (chunk.empty()) {
            break;
        }

        if (!writer.WriteSync(chunk.data(), chunk.size())) {
            if (error_message != nullptr) {
                *error_message = "Failed while writing destination binary: " + destination.string();
            }
            return false;
        }

        remaining -= chunk.size();
    }

    return true;
}

std::string ToolTo::ReadTextFile(const std::filesystem::path& path) {
    AsyncIO::IO::Async::AsyncFileReader reader;
    if (!reader.OpenFile(path.string())) {
        return std::string();
    }

    const int64_t file_size = reader.GetFileSize();
    if (file_size < 0) {
        return std::string();
    }

    std::vector<uint8_t> bytes;
    if (!reader.ReadSync(static_cast<size_t>(file_size), bytes)) {
        return std::string();
    }

    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool ToolTo::WriteTextFile(const std::filesystem::path& path,
                           const std::string& text,
                           bool append,
                           std::string* error_message) {
    AsyncIO::IO::Async::AsyncFileWriter writer;
    if (!writer.CreateFile(path.string(), append)) {
        if (error_message != nullptr) {
            *error_message = writer.GetLastError();
        }
        return false;
    }

    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    if (!writer.WriteSync(data, text.size())) {
        if (error_message != nullptr) {
            *error_message = writer.GetLastError();
        }
        return false;
    }

    return true;
}

}