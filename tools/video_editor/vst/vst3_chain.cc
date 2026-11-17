// vst3_chain.cc — see vst3_chain.h for the contract. The chain is
// deliberately defensive: every failure mode degrades to "warn once +
// bypass" so a missing/broken plugin can never take down a mix.
#include "vst/vst3_chain.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace XER::VideoEditor::Vst {
namespace {

// Label for warnings: the serialized display name when present, else
// the plugin path.
std::string NodeLabel(const ClipAudioFx& fx) {
    if (!fx.name.empty()) {
        return fx.name;
    }
    if (!fx.plugin_path.empty()) {
        return fx.plugin_path;
    }
    return "<unnamed plugin>";
}

}  // namespace

Vst3Chain::Vst3Chain(double sample_rate, std::uint32_t max_block_size,
                     PluginInstanceFactory factory, WarnFn warn)
    : sample_rate_(sample_rate > 0.0 ? sample_rate : 48000.0),
      max_block_size_(max_block_size > 0 ? max_block_size : 1u),
      factory_(std::move(factory)),
      warn_(std::move(warn)) {}

std::string Vst3Chain::Signature(
    const std::vector<ClipAudioFx>& fx_list) {
    // Unit-separator-framed fields; floats compare by bit pattern so
    // representation noise can never mask a real change.
    std::string sig;
    for (const ClipAudioFx& fx : fx_list) {
        sig.append(fx.plugin_path);
        sig.push_back('\x1f');
        sig.push_back(fx.enabled ? '1' : '0');
        sig.push_back('\x1f');
        for (const auto& [id, value] : fx.params) {
            sig.append(std::to_string(id));
            sig.push_back('=');
            sig.append(
                std::to_string(std::bit_cast<std::uint32_t>(value)));
            sig.push_back('\x1f');
        }
        sig.push_back('\x1e');
    }
    return sig;
}

void Vst3Chain::Sync(const std::vector<ClipAudioFx>& fx_list) {
    const std::string sig = Signature(fx_list);
    if (sig == signature_ && nodes_.size() == fx_list.size()) {
        return;
    }
    signature_ = sig;
    nodes_.clear();   // destroys live instances — state resets
    nodes_.reserve(fx_list.size());
    for (const ClipAudioFx& fx : fx_list) {
        Node node;
        node.spec = fx;
        nodes_.push_back(std::move(node));
    }
}

void Vst3Chain::Warn(const std::string& message) {
    if (!warned_once_.insert(message).second) {
        return;   // already reported
    }
    if (warn_ != nullptr) {
        warn_(message);
    }
}

void Vst3Chain::InstantiateNode(Node& node) {
    node.instantiate_attempted = true;
    if (factory_ == nullptr) {
        node.bypassed = true;
        Warn("clip fx '" + NodeLabel(node.spec) +
             "': no plugin factory; bypassed");
        return;
    }
    std::string error;
    node.instance = factory_(node.spec, sample_rate_, max_block_size_,
                             &error);
    if (node.instance == nullptr) {
        node.bypassed = true;
        Warn("clip fx '" + NodeLabel(node.spec) +
             "': plugin instantiation failed" +
             (error.empty() ? std::string() : ": " + error) +
             "; bypassed");
        return;
    }
    // Serialized params first, then live SetParam overrides.
    for (const auto& [id, value] : node.spec.params) {
        node.instance->SetParameter(id, value);
    }
    for (const auto& [id, value] : node.live_params) {
        node.instance->SetParameter(id, value);
    }
}

void Vst3Chain::ProcessInterleaved(float* lr, std::uint32_t frames) {
    if (lr == nullptr || frames == 0) {
        return;
    }
    for (Node& node : nodes_) {
        if (!node.spec.enabled || node.bypassed) {
            continue;
        }
        if (!node.instantiate_attempted) {
            InstantiateNode(node);
        }
        if (node.instance == nullptr) {
            continue;
        }
        // Slice to the instance's block limit — hosts reject blocks
        // larger than their configured maximum.
        bool ok = true;
        std::uint32_t offset = 0;
        while (offset < frames && ok) {
            const std::uint32_t n = std::min(
                max_block_size_, frames - offset);
            ok = node.instance->Process(lr + 2ull * offset,
                                        lr + 2ull * offset, n);
            offset += n;
        }
        if (!ok) {
            node.bypassed = true;
            node.instance.reset();
            Warn("clip fx '" + NodeLabel(node.spec) +
                 "': process failed; bypassed");
        }
    }
}

void Vst3Chain::SetParam(std::size_t node_index, std::uint32_t param_id,
                         float value) {
    if (node_index >= nodes_.size()) {
        return;
    }
    Node& node = nodes_[node_index];
    node.live_params[param_id] = value;
    if (node.instance != nullptr) {
        node.instance->SetParameter(param_id, value);
    }
}

bool Vst3Chain::NodeInstantiated(std::size_t i) const {
    return i < nodes_.size() && nodes_[i].instance != nullptr;
}

bool Vst3Chain::NodeBypassed(std::size_t i) const {
    return i < nodes_.size() && nodes_[i].bypassed;
}

std::string Vst3Chain::NodeName(std::size_t i) const {
    if (i >= nodes_.size()) {
        return {};
    }
    const Node& node = nodes_[i];
    return node.instance != nullptr ? node.instance->Name()
                                    : NodeLabel(node.spec);
}

}  // namespace XER::VideoEditor::Vst
