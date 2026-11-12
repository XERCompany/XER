#pragma once

#include "engine_params.h"

#include <filesystem>
#include <string>

namespace Xer {

struct XerSandboxSettings {
    bool enabled = false;
    std::filesystem::path root;
    bool read_only = true;
};

class XerSandbox {
public:
    XerSandbox();
    explicit XerSandbox(XerSandboxSettings settings);
    explicit XerSandbox(const XER::EngineParams& params);

    bool enabled() const { return settings_.enabled; }
    bool CanRead(const std::filesystem::path& path) const;
    bool CanWrite(const std::filesystem::path& path) const;
    std::string Describe() const;

private:
    std::filesystem::path Canonicalize(const std::filesystem::path& path) const;

    XerSandboxSettings settings_;
};

}  // namespace Xer