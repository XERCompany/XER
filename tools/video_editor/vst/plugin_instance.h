// Plugin-instance abstraction for per-clip audio FX chains
// (clip.audio_fx, Phase 2). The production implementation wraps the
// repo's VST3 host (src/audio/plugin_wrappers); unit tests inject
// fakes through this interface — the chain and mixer never touch
// VST3 types directly.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "timeline_model.h"   // ClipAudioFx

namespace XER::VideoEditor::Vst {

// One loaded audio plugin on a clip's FX chain. Stereo-interleaved
// float processor at the host (mixer) stream rate. NOT internally
// synchronized — driven by the mixer's single consumer thread.
class IPluginInstance {
public:
    virtual ~IPluginInstance() = default;

    // Processes `frames` interleaved STEREO float samples: `input` and
    // `output` each hold 2*frames floats (L,R pairs). In-place
    // operation (input == output) must be supported. Sample values are
    // linear, nominally [-1,1] — plugins may exceed that. `frames`
    // never exceeds the max_block_size the instance was built for.
    // Returns false on a processing failure — the chain warns once and
    // bypasses the node for the rest of the stream.
    virtual bool Process(const float* input, float* output,
                         std::uint32_t frames) = 0;

    // Sets a normalized parameter value (VST3 convention: [0,1]).
    virtual bool SetParameter(std::uint32_t id, float value) = 0;

    // Display name (plugin class name, or the path when unknown).
    virtual std::string Name() const = 0;
};

using PluginInstancePtr = std::unique_ptr<IPluginInstance>;

// Produces one instance for an fx node. `sample_rate` /
// `max_block_size` follow the host stream format (the mixer's
// output rate / chunk size). Returning nullptr — ideally with
// *error_out filled — means "cannot instantiate": callers bypass the
// node rather than failing the mix.
using PluginInstanceFactory = std::function<PluginInstancePtr(
    const ClipAudioFx& fx, double sample_rate,
    std::uint32_t max_block_size, std::string* error_out)>;

}  // namespace XER::VideoEditor::Vst
