#pragma once

#include "async_io/log_and_debug/io_perf_counter.h"

#include <cstdint>
#include <string>

namespace Xer {

struct XerAsyncTask {
    std::int64_t timeout_ms = 0;
};

class XerAsync {
public:
    XerAsync();

    bool Sleep(const XerAsyncTask& task, std::string* error_out = nullptr) const;
    std::int64_t NowMilliseconds() const;
    std::string Report() const;

private:
    mutable XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
};

}  // namespace Xer