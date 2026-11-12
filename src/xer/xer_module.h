#pragma once

#include "async_io/log_and_debug/io_perf_counter.h"
#include "xer/xer_value.h"

#include <absl/container/flat_hash_map.h>

#include <functional>
#include <string>
#include <vector>

namespace Xer {

struct XerModuleCallContext {
    std::string method;
    std::vector<XerValue> positional_args;
    absl::flat_hash_map<std::string, XerValue> named_args;
};

using XerModuleHandler = std::function<XerValue(const XerModuleCallContext&, std::string*)>;

class XerModuleRegistry {
public:
    XerModuleRegistry();

    void Register(std::string name, XerModuleHandler handler);
    bool Has(const std::string& name) const;
    XerValue Invoke(const std::string& name, const XerModuleCallContext& context, std::string* error_out = nullptr) const;
    std::vector<std::string> Names() const;
    std::string Report() const;

private:
    mutable XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
    absl::flat_hash_map<std::string, XerModuleHandler> handlers_;
};

}  // namespace Xer