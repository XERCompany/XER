/**
 * BufferedDataSource — forward-read buffering layer between a
 * DataSource (file/URL) and a Demuxer (design doc: multibuffer absorbs
 * network/file latency; the demuxer reads through a Read() interface
 * at a cursor; backward seeks inside the buffered window are free).
 *
 * Model: a byte window |window_| covers source range
 * [window_start_, window_start_ + window_size_). Sequential reads fill
 * the window ahead of the cursor; a cursor inside the window is served
 * without touching the DataSource; anything else falls back to
 * DataSource::ReadAt.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/demuxer/data_source.h"

namespace XER::Media {

class BufferedDataSource {
public:
    BufferedDataSource(std::unique_ptr<DataSource> source,
                       std::size_t forward_bytes = 512 * 1024,
                       std::size_t backward_bytes = 256 * 1024);
    ~BufferedDataSource();

    enum class Status : std::uint8_t { kOk, kEos, kError };

    // Reads |size| bytes at the read cursor into |out|.
    Status Read(std::uint8_t* out, std::size_t size,
                std::size_t* read_out);

    // Repositions the read cursor.
    void Seek(std::uint64_t position) { read_pos_ = position; }

    std::uint64_t position() const { return read_pos_; }
    std::int64_t size() const { return source_->size(); }
    bool streaming() const { return source_->streaming(); }
    bool eof() const { return eos_; }
    const std::string& last_error() const { return last_error_; }

private:
    // Fills the window so it covers [read_pos_, read_pos_+size) when
    // possible. Returns kEos at end of source, kError on read failure.
    Status EnsureBuffered(std::size_t size);
    // Streams source bytes into the window's free tail until full/EOF.
    Status Fill();

    std::unique_ptr<DataSource> source_;
    std::vector<std::uint8_t> window_;
    std::uint64_t window_start_ = 0;
    std::size_t window_size_ = 0;  // valid bytes in window_
    std::uint64_t read_pos_ = 0;
    std::size_t forward_bytes_;
    std::size_t backward_bytes_;
    bool eos_ = false;
    std::string last_error_;
};

}  // namespace XER::Media
