#include "media/demuxer/file_data_source.h"

namespace XER::Media {

FileDataSource::~FileDataSource() {
    if (file_) std::fclose(file_);
}

std::unique_ptr<FileDataSource> FileDataSource::Open(
    const std::string& path, std::string* error_out) {
    auto ds = std::unique_ptr<FileDataSource>(new FileDataSource);
    ds->file_ = std::fopen(path.c_str(), "rb");
    if (!ds->file_) {
        if (error_out) *error_out = "cannot open " + path;
        return nullptr;
    }
    std::fseek(ds->file_, 0, SEEK_END);
    ds->size_ = std::ftell(ds->file_);
    std::fseek(ds->file_, 0, SEEK_SET);
    return ds;
}

DataSource::Status FileDataSource::ReadAt(std::uint64_t offset,
                                          std::uint8_t* out,
                                          std::size_t size,
                                          std::size_t* read_out) {
    if (read_out) *read_out = 0;
    if (!file_) return Status::kError;
    if (offset >= static_cast<std::uint64_t>(size_)) return Status::kEos;
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0)
        return Status::kError;
    const std::size_t n = std::fread(out, 1, size, file_);
    if (read_out) *read_out = n;
    return n == 0 ? Status::kEos : Status::kOk;
}

DataSource* DataSource::CreateFileSource(const std::string& path,
                                         std::string* error_out) {
    // Caller takes ownership via the factory wrapper below.
    return FileDataSource::Open(path, error_out).release();
}

}  // namespace XER::Media
