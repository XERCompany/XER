#pragma once
#include "loader_base.h"
#include <memory>
#include <future>
#include <string>

#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

namespace XER::ML::Loaders {

/// Async prefetch wrapper — enqueues load onto IO::AsyncIO::IOThreadPool,
/// returns result via std::future<Dataset>.
class LoaderPrefetcher {
public:
    explicit LoaderPrefetcher(std::unique_ptr<LoaderBase> inner)
        : inner_(std::move(inner)) {}

    /// Enqueue an async load onto the shared IO thread pool
    void Prefetch(const std::string& path);
    /// Block until the enqueued load completes and return the result
    Dataset Collect();

private:
    std::unique_ptr<LoaderBase> inner_;
    std::future<Dataset>         future_;
};

}  // namespace XER::ML::Loaders
