// Per-clip ordered audio-plugin chain — the runtime counterpart of
// Clip::audio_fx (Phase 2). Owns one node per ClipAudioFx entry,
// instantiates plugins lazily on the first processed chunk, and
// processes interleaved stereo float buffers in place.
//
// Failure contract: a node whose plugin cannot be instantiated, or
// whose Process() fails, warns ONCE (via the WarnFn) and is bypassed
// for the rest of the stream — the chain never throws and never fails
// the mix; audio passes through the remaining nodes unchanged.
//
// Rebuild semantics: Sync() compares a serialized signature of the fx
// list (path + enabled + params — name is display-only) and rebuilds
// the node list only when it differs, so the mixer can call it per
// chunk at trivial cost. A rebuild destroys plugin state.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "timeline_model.h"        // ClipAudioFx
#include "vst/plugin_instance.h"   // IPluginInstance / PluginInstanceFactory

namespace XER::VideoEditor::Vst {

class Vst3Chain {
public:
    // Non-fatal diagnostics — the mixer forwards these into
    // AudioMixer::Warnings().
    using WarnFn = std::function<void(const std::string&)>;

    // `factory` may be null — every node then bypasses with a warning
    // on first process (useful for tests / plugin-less builds).
    Vst3Chain(double sample_rate, std::uint32_t max_block_size,
              PluginInstanceFactory factory, WarnFn warn = {});

    // Stable, comparable serialization of an fx list. Covers
    // plugin_path, enabled, and the params map (by float bit pattern);
    // `name` is display-only and excluded.
    static std::string Signature(const std::vector<ClipAudioFx>& fx_list);

    // Rebuilds the node list iff Signature(fx_list) differs from the
    // last synced signature — a cheap no-op when unchanged. Live
    // tweaks made via SetParam are lost on rebuild (the new spec is
    // authoritative).
    void Sync(const std::vector<ClipAudioFx>& fx_list);

    // Processes `frames` stereo-interleaved samples (2*frames floats)
    // in place through every enabled, non-bypassed node, in list
    // order. Lazily instantiates nodes on first use; instantiation and
    // per-node failures warn once + bypass (audio unmodified).
    void ProcessInterleaved(float* lr, std::uint32_t frames);

    // Live parameter tweak for node `node_index`: applied immediately
    // when the instance exists, otherwise recorded and applied on
    // instantiation. Out-of-range indices are ignored.
    void SetParam(std::size_t node_index, std::uint32_t param_id,
                  float value);

    // Introspection (tests / UI).
    std::size_t NodeCount() const { return nodes_.size(); }
    bool NodeInstantiated(std::size_t i) const;
    bool NodeBypassed(std::size_t i) const;
    std::string NodeName(std::size_t i) const;
    const std::string& ActiveSignature() const { return signature_; }

private:
    struct Node {
        ClipAudioFx spec;
        // SetParam overrides layered over spec.params at instantiation.
        std::map<std::uint32_t, float> live_params;
        PluginInstancePtr instance;
        bool instantiate_attempted = false;
        bool bypassed = false;
    };

    void Warn(const std::string& message);
    void InstantiateNode(Node& node);

    double sample_rate_;
    std::uint32_t max_block_size_;
    PluginInstanceFactory factory_;
    WarnFn warn_;
    std::vector<Node> nodes_;
    std::string signature_;
    std::set<std::string> warned_once_;   // warn-once per message
};

}  // namespace XER::VideoEditor::Vst
