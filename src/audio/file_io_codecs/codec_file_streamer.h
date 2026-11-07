#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "async_io/async_file_reader.h"
#include "async_io/async_file_writer.h"
#include "async_io/log_and_debug/io_perf_counter.h"

namespace Engine::Audio::CodecIO {

class CodecFileStreamer {
public:
    CodecFileStreamer();
    ~CodecFileStreamer();

    bool OpenRead(const std::string& path);
    bool OpenWrite(const std::string& path);
    size_t ReadBytes(uint8_t* dst, size_t bytes);
    size_t WriteBytes(const uint8_t* src, size_t bytes);
    void Close();

private:
    AsyncIO::IO::Async::AsyncFileReader reader_;
    AsyncIO::IO::Async::AsyncFileWriter writer_;
    AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
    bool reading_open_;
    bool writing_open_;
};

}  // namespace Engine::Audio::CodecIO
