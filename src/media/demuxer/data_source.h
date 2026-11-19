/**
 * DataSource — random-access byte source between the network/file layer
 * and the demuxer (design doc). Range reads feed the SeekableBuffer;
 * size may be unknown for live streams.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace XER::Media {

class DataSource {
public:
    virtual ~DataSource() = default;

    enum class Status : std::uint8_t { kOk, kEos, kError };

    // Blocking range read; fills |out| at absolute |offset|.
    virtual Status ReadAt(std::uint64_t offset, std::uint8_t* out,
                          std::size_t size, std::size_t* read_out) = 0;
    // Total size in bytes; -1 when unknown (live).
    virtual std::int64_t size() const = 0;
    virtual bool streaming() const = 0;

    // Creates a file-backed DataSource for |path|.
    static DataSource* CreateFileSource(const std::string& path,
                                        std::string* error_out);
};

}  // namespace XER::Media
