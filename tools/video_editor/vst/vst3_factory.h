// Production VST3 backend for clip FX chains — a thin wrap over the
// repo's plugin_wrappers host (Vst3HostInterface) and scanner
// (Vst3PluginScanner). No VST3 plumbing is reimplemented here; this
// file only adapts the host's mono Process() to the stereo
// IPluginInstance contract and re-exports plugin discovery.
//
// Stereo note: Vst3HostInterface::Process() is MONO (the plugin's
// main stereo bus, first channel). To preserve L/R imaging a
// Vst3PluginInstance owns TWO host objects loaded with the same
// plugin — dual-mono, one per channel.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "audio/plugin_wrappers/vst3_host_interface.h"
#include "audio/plugin_wrappers/vst3_plugin_scanner.h"
#include "vst/plugin_instance.h"

namespace XER::VideoEditor::Vst {

// 'Available plugins' list for UI pickers. `roots` empty ->
// DefaultVst3SearchRoots() (%CommonProgramFiles%\VST3 etc.).
// Scanning loads each candidate module's factory — call from the UI
// thread, never the audio thread (see vst3_plugin_scanner.h).
std::vector<XER::Audio::Plugin::Vst3PluginInfo> ScanAvailableVst3Plugins(
    const std::vector<std::string>& roots = {},
    std::string* error_out = nullptr);

// Stereo IPluginInstance over two mono Vst3HostInterface objects
// (dual-mono). Process() deinterleaves, runs each channel through its
// own host, and re-interleaves — in-place safe.
class Vst3PluginInstance final : public IPluginInstance {
public:
    // Loads `plugin_path` (.vst3 bundle dir or module file, or a
    // builtin:// reference) into both channel hosts at
    // `sample_rate` / `max_block_size`. Returns nullptr + error_out on
    // failure (missing file, unsupported plugin, host init failure).
    static std::unique_ptr<Vst3PluginInstance> Create(
        const std::string& plugin_path, double sample_rate,
        std::uint32_t max_block_size, std::string* error_out = nullptr);

    ~Vst3PluginInstance() override = default;

    bool Process(const float* input, float* output,
                 std::uint32_t frames) override;
    bool SetParameter(std::uint32_t id, float value) override;
    std::string Name() const override;

    // The left channel's host — exposes the plugin's native editor
    // view (IPlugView) via HasPluginEditor/CreatePluginEditor/
    // AttachPluginEditor when the plugin provides one.
    XER::Audio::Plugin::Vst3HostInterface* PrimaryHost() const {
        return left_.get();
    }

private:
    Vst3PluginInstance(
        std::unique_ptr<XER::Audio::Plugin::Vst3HostInterface> left,
        std::unique_ptr<XER::Audio::Plugin::Vst3HostInterface> right,
        std::string name);

    std::unique_ptr<XER::Audio::Plugin::Vst3HostInterface> left_;
    std::unique_ptr<XER::Audio::Plugin::Vst3HostInterface> right_;
    std::string name_;
    // Deinterleave/interleave scratch — resized per Process call.
    std::vector<float> in_l_, in_r_, out_l_, out_r_;
};

// Vst3Chain-compatible factory for production: instantiates
// fx.plugin_path through Vst3PluginInstance::Create. Parameters in
// fx.params are NOT applied here — the chain applies them on
// instantiation so fakes and production share one path.
PluginInstancePtr InstantiateVst3Plugin(
    const ClipAudioFx& fx, double sample_rate,
    std::uint32_t max_block_size, std::string* error_out);

}  // namespace XER::VideoEditor::Vst
