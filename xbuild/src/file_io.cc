#include "file_io.h"

#include <absl/cleanup/cleanup.h>
#include <absl/strings/str_cat.h>
#include <uv.h>

#include <array>
#include <fcntl.h>
#include <utility>

namespace XBuild {
namespace {

// Every libuv filesystem request owns allocations, including failed requests.
struct FileRequest {
    uv_fs_t value{};
    FileRequest() = default;
    FileRequest(const FileRequest&) = delete;
    FileRequest& operator=(const FileRequest&) = delete;
    ~FileRequest() { uv_fs_req_cleanup(&value); }
};

} // namespace

bool ReadFile(const std::filesystem::path& path, std::string& output, std::string& error) {
    error.clear();
    const auto utf8 = path.u8string();
    const std::string filename(utf8.begin(), utf8.end());
    FileRequest open_request;
    const int file = uv_fs_open(nullptr, &open_request.value, filename.c_str(), O_RDONLY, 0, nullptr);
    if (file < 0) {
        error = absl::StrCat("Cannot open project configuration: ", filename, ": ", uv_strerror(file));
        return false;
    }
    absl::Cleanup close_file = [file] {
        FileRequest close_request;
        uv_fs_close(nullptr, &close_request.value, file, nullptr);
    };

    std::string contents;
    std::array<char, 64 * 1024> buffer{};
    for (;;) {
        FileRequest read_request;
        const uv_buf_t chunk = uv_buf_init(buffer.data(), static_cast<unsigned int>(buffer.size()));
        const int count = uv_fs_read(nullptr, &read_request.value, file, &chunk, 1, -1, nullptr);
        if (count < 0) {
            error = absl::StrCat("Cannot read project configuration: ", filename, ": ", uv_strerror(count));
            return false;
        }
        if (count == 0) {
            break;
        }
        contents.append(buffer.data(), static_cast<std::size_t>(count));
    }
    output = std::move(contents);
    return true;
}

} // namespace XBuild
