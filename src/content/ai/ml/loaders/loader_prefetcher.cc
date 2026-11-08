#include "loader_prefetcher.h"

#include <memory>
#include <future>
#include <stdexcept>

#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#include "async_io/io_thread_pool.h"

namespace XER::ML::Loaders {

void LoaderPrefetcher::Prefetch(const std::string& path) {
    auto prom = std::make_shared<std::promise<Dataset>>();
    future_ = prom->get_future();
    LoaderBase* loader = inner_.get();
    IO::AsyncIO::IOThreadPool::GetSharedInstance().Enqueue(
        [loader, path, prom]() mutable {
            try {
                prom->set_value(loader->Load(path));
            } catch (...) {
                prom->set_exception(std::current_exception());
            }
        });
}

Dataset LoaderPrefetcher::Collect() {
    return future_.get();
}

}  // namespace XER::ML::Loaders
