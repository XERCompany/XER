/**
 * Bounded producer/consumer queue used by DemuxerStream readers and the
 * audio tap between the demuxer and decoder threads. Non-blocking:
 * Push fails when full, Pop returns false when empty.
 */
#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace XER::Media {

template <typename T>
class BufferQueue {
public:
    explicit BufferQueue(std::size_t capacity) : capacity_(capacity) {}

    bool Push(T item) {
        std::lock_guard lock(mu_);
        if (items_.size() >= capacity_) {
            // Drop the oldest — the tap queue semantics keep realtime
            // playback from stalling on a slow consumer.
            items_.pop_front();
        }
        items_.push_back(std::move(item));
        return true;
    }

    bool TryPush(T item) {
        std::lock_guard lock(mu_);
        if (items_.size() >= capacity_) return false;
        items_.push_back(std::move(item));
        return true;
    }

    std::optional<T> Pop() {
        std::lock_guard lock(mu_);
        if (items_.empty()) return std::nullopt;
        T v = std::move(items_.front());
        items_.pop_front();
        return v;
    }

    std::size_t size() const {
        std::lock_guard lock(mu_);
        return items_.size();
    }
    bool empty() const { return size() == 0; }
    std::size_t capacity() const { return capacity_; }

    void Clear() {
        std::lock_guard lock(mu_);
        items_.clear();
    }

private:
    const std::size_t capacity_;
    mutable std::mutex mu_;
    std::deque<T> items_;
};

}  // namespace XER::Media
