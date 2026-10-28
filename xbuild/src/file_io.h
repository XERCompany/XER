#ifndef XBUILD_FILE_IO_H
#define XBUILD_FILE_IO_H

#include <filesystem>
#include <string>

namespace XBuild {

// Read a file through libuv's synchronous filesystem API.
// On failure, output is unchanged and error includes the path and OS error.
bool ReadFile(const std::filesystem::path& path, std::string& output, std::string& error);

} // namespace XBuild

#endif // XBUILD_FILE_IO_H
