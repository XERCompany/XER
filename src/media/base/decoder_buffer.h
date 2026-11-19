/**
 * DecoderBuffer — one compressed unit fed to a Decoder: timestamped,
 * optionally key-framed, carrying codec-extradata side info.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "media/base/media_types.h"

namespace XER::Media {

class DecoderBuffer {
public:
    DecoderBuffer() = default;
    explicit DecoderBuffer(std::vector<std::uint8_t> data,
                           TimeTicks timestamp = kNoTimestamp,
                           TimeTicks duration = 0)
        : data_(std::move(data)),
          timestamp_(timestamp),
          duration_(duration) {}

    static DecoderBuffer EOS() {
        DecoderBuffer b;
        b.eos_ = true;
        return b;
    }

    const std::uint8_t* data() const {
        return data_.empty() ? nullptr : data_.data();
    }
    std::size_t size() const { return data_.size(); }
    std::vector<std::uint8_t>& mutable_data() { return data_; }

    TimeTicks timestamp() const { return timestamp_; }
    void set_timestamp(TimeTicks t) { timestamp_ = t; }
    TimeTicks duration() const { return duration_; }
    void set_duration(TimeTicks d) { duration_ = d; }

    bool is_key_frame() const { return key_frame_; }
    void set_key_frame(bool k) { key_frame_ = k; }
    bool end_of_stream() const { return eos_; }

    const std::vector<std::uint8_t>& side_data() const { return side_data_; }
    void set_side_data(std::vector<std::uint8_t> d) {
        side_data_ = std::move(d);
    }

private:
    std::vector<std::uint8_t> data_;
    std::vector<std::uint8_t> side_data_;
    TimeTicks timestamp_ = kNoTimestamp;
    TimeTicks duration_ = 0;
    bool key_frame_ = false;
    bool eos_ = false;
};

}  // namespace XER::Media
