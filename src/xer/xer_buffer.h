#pragma once

#include "helper/buffer.h"
#include "helper/tool_to.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Xer {

class XerBuffer {
public:
    XerBuffer() = default;
    explicit XerBuffer(std::vector<std::uint8_t> bytes);

    static XerBuffer FromString(std::string_view text);
    static XerBuffer FromFile(const std::filesystem::path& path, std::string* error_out = nullptr);

    void Append(std::string_view text);
    void Append(const std::uint8_t* data, std::size_t size);

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    std::size_t size() const { return bytes_.size(); }
    bool empty() const { return bytes_.empty(); }

    std::string ToString() const;

private:
    std::vector<std::uint8_t> bytes_;
};

}  // namespace Xer