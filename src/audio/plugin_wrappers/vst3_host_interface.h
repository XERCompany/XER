#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "async_io/log_and_debug/io_perf_counter.h"
#include "plugin_builtin_host.h"

namespace XER::Engine::Audio::Plugin {

class Vst3ExternalHost;

class Vst3HostInterface {
public:
	// Out-of-line: external_host_ holds an incomplete type in this header, so
	// every special member that could destroy it must be defined in the .cc.
	Vst3HostInterface();
	~Vst3HostInterface();
	Vst3HostInterface(const Vst3HostInterface&) = delete;
	Vst3HostInterface& operator=(const Vst3HostInterface&) = delete;

	enum class BackendMode {
		kNone,
		kBuiltin,
		kExternal,
		kUnsupportedExternal,
	};

    bool Initialize(double sample_rate, uint32_t max_block_size);
    bool LoadPlugin(const std::string& plugin_path);
    bool Process(const float* input, float* output, uint32_t frames);
    bool SetParameter(uint32_t id, float value);
    bool GetParameter(uint32_t id, float* value) const;
    std::vector<PluginParameterInfo> GetParameters() const;
    std::string GetLoadedPluginId() const;
	std::string GetBackendMode() const;
	std::string GetPluginName() const;

	// Native editor view (IPlugView). Only external plugins can provide one.
	bool HasPluginEditor() const;
	bool CreatePluginEditor(std::string* error_out = nullptr);
	bool AttachPluginEditor(void* parent_hwnd, int left, int top, int right, int bottom,
	                        std::string* error_out = nullptr);
	void SetPluginEditorFrame(int left, int top, int right, int bottom);
	void RemovePluginEditor();

private:
    XER::AsyncIO::IO::LogDebug::PerformanceCounter perf_counter_;
    BuiltinPluginHost builtin_host_;
    std::unique_ptr<Vst3ExternalHost> external_host_;
    double sample_rate_ = 44100.0;
    uint32_t max_block_size_ = 512;
    std::string plugin_path_;
	BackendMode backend_mode_ = BackendMode::kNone;
};

}  // namespace XER::Engine::Audio::Plugin
