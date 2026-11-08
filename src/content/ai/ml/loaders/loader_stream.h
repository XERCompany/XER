#pragma once
#include <vector>
#include <functional>
#include <string>

namespace XER::ML::Loaders {

/// Streaming loader interface: calls callback for each float row chunk
class LoaderStream {
public:
    using RowCallback = std::function<void(std::vector<float>)>;

    explicit LoaderStream(RowCallback cb) : callback_(std::move(cb)) {}

    /// Stream rows from file, invoking callback for each
    void Stream(const std::string& path);

private:
    RowCallback callback_;
};

}  // namespace XER::ML::Loaders
