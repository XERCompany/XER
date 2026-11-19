/**
 * FileDataSource — DataSource over a local file (seekable, known size).
 */
#pragma once

#include <cstdio>
#include <memory>
#include <string>

#include "media/demuxer/data_source.h"

namespace XER::Media {

class FileDataSource final : public DataSource {
public:
    ~FileDataSource() override;
    // Factory: nullptr + error_out on failure.
    static std::unique_ptr<FileDataSource> Open(const std::string& path,
                                                std::string* error_out);

    Status ReadAt(std::uint64_t offset, std::uint8_t* out,
                  std::size_t size, std::size_t* read_out) override;
    std::int64_t size() const override { return size_; }
    bool streaming() const override { return false; }

private:
    FileDataSource() = default;
    FILE* file_ = nullptr;
    std::int64_t size_ = -1;
};

}  // namespace XER::Media
