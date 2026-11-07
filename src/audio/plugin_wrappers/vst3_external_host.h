#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "plugin_parameter_bridge.h"

namespace XER::Engine::Audio::Plugin {

// Real VST3 plugin host backed by the Steinberg SDK (vcpkg vst3sdk).
// Loads external .vst3 bundles, processes mono float buffers through the
// plugin's main stereo bus, and exposes normalized parameters via the
// plugin's IEditController.
class Vst3ExternalHost {
public:
    Vst3ExternalHost();
    ~Vst3ExternalHost();

    Vst3ExternalHost(const Vst3ExternalHost&) = delete;
    Vst3ExternalHost& operator=(const Vst3ExternalHost&) = delete;

    bool Load(const std::string& plugin_path, double sample_rate,
              uint32_t max_block_size, std::string* error_out = nullptr);
    void Unload();
    bool IsLoaded() const;

    // Mono in -> mono out (first channel of the first output bus).
    bool Process(const float* input, float* output, uint32_t frames);

    // Parameter values are normalized [0, 1] per the VST3 spec.
    bool SetParameter(uint32_t id, float value);
    bool GetParameter(uint32_t id, float* value) const;
    std::vector<PluginParameterInfo> GetParameters() const;

    std::string GetPluginName() const;

    // Native editor view (IPlugView). On Windows the parent is a HWND and
    // rects are client-area pixels of that window.
    bool HasEditorView() const;
    bool CreateEditorView(std::string* error_out = nullptr);
    bool AttachEditorView(void* parent_hwnd, int left, int top, int right, int bottom,
                          std::string* error_out = nullptr);
    void SetEditorFrame(int left, int top, int right, int bottom);
    void RemoveEditorView();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::Engine::Audio::Plugin
