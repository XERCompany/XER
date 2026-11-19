#include "media/demuxer/buffered_data_source.h"

#include <algorithm>
#include <cstring>

namespace XER::Media {

BufferedDataSource::BufferedDataSource(std::unique_ptr<DataSource> source,
                                       std::size_t forward_bytes,
                                       std::size_t backward_bytes)
    : source_(std::move(source)),
      window_(forward_bytes),
      forward_bytes_(forward_bytes),
      backward_bytes_(backward_bytes) {}

BufferedDataSource::~BufferedDataSource() = default;

BufferedDataSource::Status BufferedDataSource::Read(
    std::uint8_t* out, std::size_t size, std::size_t* read_out) {
    *read_out = 0;
    if (size == 0) return Status::kOk;

    const auto st = EnsureBuffered(size);
    if (st == Status::kError) return st;

    // Serve from the window if the cursor is inside it.
    if (read_pos_ >= window_start_ &&
        read_pos_ < window_start_ + window_size_) {
        const std::size_t off =
            static_cast<std::size_t>(read_pos_ - window_start_);
        const std::size_t avail = window_size_ - off;
        const std::size_t n = std::min(size, avail);
        std::memcpy(out, window_.data() + off, n);
        read_pos_ += n;
        *read_out = n;
        return n == size ? Status::kOk : Status::kEos;
    }
    // Cursor outside window → direct range read.
    std::size_t n = 0;
    const auto ds = source_->ReadAt(read_pos_, out, size, &n);
    read_pos_ += n;
    *read_out = n;
    if (ds == DataSource::Status::kError) {
        last_error_ = "data source read failed";
        return Status::kError;
    }
    return n == size ? Status::kOk : Status::kEos;
}

BufferedDataSource::Status BufferedDataSource::EnsureBuffered(
    std::size_t size) {
    // Nothing to do when the window already covers the request.
    if (read_pos_ >= window_start_ &&
        read_pos_ + size <= window_start_ + window_size_)
        return Status::kOk;

    // Cursor before window start or beyond forward reach → re-anchor
    // the window at the cursor (keeps backward_bytes_ history only
    // when the cursor is near the window edge).
    if (read_pos_ < window_start_ ||
        read_pos_ > window_start_ + window_size_ + backward_bytes_) {
        window_start_ = read_pos_;
        window_size_ = 0;
        // Preserve trailing bytes the cursor may still need behind it.
        return Fill();
    }

    // Cursor inside window but request extends past buffered tail:
    // compact consumed bytes forward, then fill.
    const std::size_t used =
        static_cast<std::size_t>(read_pos_ - window_start_);
    const std::size_t keep_back =
        std::min(used, backward_bytes_);
    const std::size_t shift = used - keep_back;
    if (shift) {
        std::memmove(window_.data(), window_.data() + shift,
                     window_size_ - shift);
        window_start_ += shift;
        window_size_ -= shift;
    }
    return Fill();
}

BufferedDataSource::Status BufferedDataSource::Fill() {
    while (window_size_ < forward_bytes_ && !eos_) {
        const std::uint64_t off = window_start_ + window_size_;
        const std::size_t want = forward_bytes_ - window_size_;
        std::size_t got = 0;
        const auto st = source_->ReadAt(
            off, window_.data() + window_size_, want, &got);
        if (st == DataSource::Status::kError) {
            last_error_ = "data source read failed";
            return Status::kError;
        }
        window_size_ += got;
        if (st == DataSource::Status::kEos || got == 0) eos_ = true;
    }
    return Status::kOk;
}

}  // namespace XER::Media
