// PXER global hooks integration (universal template)
#pragma once
#include "native/plugin/pxer/pxer_host.h"
#include <string>
#include <unordered_map>

namespace pxer_hooks {

// Call this on any core event you want plugins to intercept
inline void CallAll(const std::string& event, void* data = nullptr) {
    auto& pxer = XER::Native::Plugin::Pxer::PluginHost::Shared();
    for (const auto& entry : pxer.GetGlobalHooks()) {
        using HookFn = int(*)(const char*, void*);
        HookFn fn = reinterpret_cast<HookFn>(entry.second);
        fn(event.c_str(), data);
    }
}

} // namespace pxer_hooks
