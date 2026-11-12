#include "xer/xer_sandbox.h"

#include <absl/strings/str_cat.h>

namespace Xer {

XerSandbox::XerSandbox() = default;

XerSandbox::XerSandbox(XerSandboxSettings settings) : settings_(std::move(settings)) {}

XerSandbox::XerSandbox(const XER::EngineParams& params) {
    settings_.enabled = params.sandbox;
    settings_.root = params.module_root.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(params.module_root);
    settings_.read_only = params.is_dry_run();
}

bool XerSandbox::CanRead(const std::filesystem::path& path) const {
    if (!settings_.enabled) {
        return true;
    }
    const auto root = Canonicalize(settings_.root.empty() ? std::filesystem::current_path() : settings_.root);
    const auto target = Canonicalize(path);
    return target.string().rfind(root.string(), 0) == 0;
}

bool XerSandbox::CanWrite(const std::filesystem::path& path) const {
    if (!settings_.enabled) {
        return true;
    }
    if (settings_.read_only) {
        return false;
    }
    return CanRead(path);
}

std::string XerSandbox::Describe() const {
    return absl::StrCat(
        "sandbox(enabled=",
        settings_.enabled ? "true" : "false",
        ", root=",
        (settings_.root.empty() ? std::filesystem::current_path() : settings_.root).string(),
        ", read_only=",
        settings_.read_only ? "true" : "false",
        ")");
}

std::filesystem::path XerSandbox::Canonicalize(const std::filesystem::path& path) const {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) {
        return canonical;
    }
    return path.lexically_normal();
}

}  // namespace Xer