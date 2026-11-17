// vst3_factory.cc — see vst3_factory.h. Adapts the repo's mono
// Vst3HostInterface to the stereo IPluginInstance contract via
// dual-mono hosting (one host object per channel).
#include "vst/vst3_factory.h"

#include <utility>

namespace XER::VideoEditor::Vst {
namespace {

namespace Plugin = XER::Audio::Plugin;

std::unique_ptr<Plugin::Vst3HostInterface> MakeHost(
    const std::string& plugin_path, double sample_rate,
    std::uint32_t max_block_size, std::string* error_out) {
    auto host = std::make_unique<Plugin::Vst3HostInterface>();
    if (!host->Initialize(sample_rate, max_block_size)) {
        if (error_out != nullptr) {
            *error_out = "host initialize failed";
        }
        return nullptr;
    }
    if (!host->LoadPlugin(plugin_path)) {
        if (error_out != nullptr) {
            *error_out = "plugin load failed: " + plugin_path;
        }
        return nullptr;
    }
    return host;
}

}  // namespace

std::vector<XER::Audio::Plugin::Vst3PluginInfo> ScanAvailableVst3Plugins(
    const std::vector<std::string>& roots, std::string* error_out) {
    const std::vector<std::string> effective =
        roots.empty() ? Plugin::DefaultVst3SearchRoots() : roots;
    return Plugin::ScanVst3Plugins(effective, error_out);
}

Vst3PluginInstance::Vst3PluginInstance(
    std::unique_ptr<Plugin::Vst3HostInterface> left,
    std::unique_ptr<Plugin::Vst3HostInterface> right, std::string name)
    : left_(std::move(left)),
      right_(std::move(right)),
      name_(std::move(name)) {}

std::unique_ptr<Vst3PluginInstance> Vst3PluginInstance::Create(
    const std::string& plugin_path, double sample_rate,
    std::uint32_t max_block_size, std::string* error_out) {
    std::string error;
    auto left = MakeHost(plugin_path, sample_rate, max_block_size,
                         &error);
    if (left == nullptr) {
        if (error_out != nullptr) {
            *error_out = error;
        }
        return nullptr;
    }
    auto right = MakeHost(plugin_path, sample_rate, max_block_size,
                          &error);
    if (right == nullptr) {
        if (error_out != nullptr) {
            *error_out = error;
        }
        return nullptr;
    }
    std::string name = left->GetPluginName();
    if (name.empty()) {
        name = left->GetLoadedPluginId();
    }
    if (name.empty()) {
        name = plugin_path;
    }
    return std::unique_ptr<Vst3PluginInstance>(new Vst3PluginInstance(
        std::move(left), std::move(right), std::move(name)));
}

bool Vst3PluginInstance::Process(const float* input, float* output,
                                 std::uint32_t frames) {
    if (input == nullptr || output == nullptr || frames == 0) {
        return false;
    }
    // Deinterleave -> per-channel mono host -> re-interleave. Scratch
    // buffers make input == output (in-place) safe.
    in_l_.resize(frames);
    in_r_.resize(frames);
    out_l_.resize(frames);
    out_r_.resize(frames);
    for (std::uint32_t i = 0; i < frames; ++i) {
        in_l_[i] = input[2ull * i];
        in_r_[i] = input[2ull * i + 1u];
    }
    if (!left_->Process(in_l_.data(), out_l_.data(), frames)) {
        return false;
    }
    if (!right_->Process(in_r_.data(), out_r_.data(), frames)) {
        return false;
    }
    for (std::uint32_t i = 0; i < frames; ++i) {
        output[2ull * i] = out_l_[i];
        output[2ull * i + 1u] = out_r_[i];
    }
    return true;
}

bool Vst3PluginInstance::SetParameter(std::uint32_t id, float value) {
    const bool l = left_ != nullptr && left_->SetParameter(id, value);
    const bool r = right_ != nullptr && right_->SetParameter(id, value);
    return l && r;
}

std::string Vst3PluginInstance::Name() const { return name_; }

PluginInstancePtr InstantiateVst3Plugin(
    const ClipAudioFx& fx, double sample_rate,
    std::uint32_t max_block_size, std::string* error_out) {
    return Vst3PluginInstance::Create(fx.plugin_path, sample_rate,
                                      max_block_size, error_out);
}

}  // namespace XER::VideoEditor::Vst
