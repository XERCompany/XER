#include "codec_file_streamer.h"

#include <cstring>

namespace XER::Engine::Audio::CodecIO {

CodecFileStreamer::CodecFileStreamer()
    : reading_open_(false), writing_open_(false) {
    perf_counter_.Enable();
}

CodecFileStreamer::~CodecFileStreamer() {
    Close();
}

bool CodecFileStreamer::OpenRead(const std::string& path) {
    reading_open_ = reader_.OpenFile(path);
    return reading_open_;
}

bool CodecFileStreamer::OpenWrite(const std::string& path) {
    writing_open_ = writer_.CreateFile(path, true);
    return writing_open_;
}

size_t CodecFileStreamer::ReadBytes(uint8_t* dst, size_t bytes) {
    if (!reading_open_ || dst == nullptr || bytes == 0) {
        return 0;
    }

    std::vector<uint8_t> buffer;
    perf_counter_.StartCounter("codec_stream_read");
    const bool ok = reader_.ReadSync(bytes, buffer);
    perf_counter_.StopCounter("codec_stream_read");
    if (!ok || buffer.empty()) {
        return 0;
    }

    std::memcpy(dst, buffer.data(), buffer.size());
    return buffer.size();
}

size_t CodecFileStreamer::WriteBytes(const uint8_t* src, size_t bytes) {
    if (!writing_open_ || src == nullptr || bytes == 0) {
        return 0;
    }
    perf_counter_.StartCounter("codec_stream_write");
    const size_t written = writer_.WriteSync(src, bytes);
    perf_counter_.StopCounter("codec_stream_write");
    return written;
}

void CodecFileStreamer::Close() {
    reader_.CloseFile();
    writer_.CloseFile();
    reading_open_ = false;
    writing_open_ = false;
}

}  // namespace XER::Engine::Audio::CodecIO
