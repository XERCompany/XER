/**
 * RAWPtr<T> / RawSpan<T> — non-owning pointer/span wrappers (design
 * doc references XER::Helper::RAWPtr and RawSpan for
 * performance-sensitive paths: zero overhead over T*, explicit about
 * non-ownership).
 */
#pragma once

#include <cstddef>

namespace XER::Helper {

template <typename T>
class RAWPtr {
public:
    RAWPtr() = default;
    RAWPtr(std::nullptr_t) {}
    RAWPtr(T* ptr) : ptr_(ptr) {}

    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    void reset(T* p = nullptr) { ptr_ = p; }

private:
    T* ptr_ = nullptr;
};

// RawSpan — non-owning (ptr,size) view for per-sample hot loops.
template <typename T>
struct RawSpan {
    T* data = nullptr;
    std::size_t size = 0;
    T& operator[](std::size_t i) const { return data[i]; }
    T* begin() const { return data; }
    T* end() const { return data + size; }
    bool empty() const { return size == 0; }
};

}  // namespace XER::Helper
