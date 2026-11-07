#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "clap_minimal.h"
#include "plugin_parameter_bridge.h"

namespace XER::Engine::Audio::Plugin {

struct ClapPluginMetadata {
	std::string id;
	std::string title;
	std::string vendor;
	std::string version;
	std::string description;
	std::string plugin_path;
	std::string loaded_identifier;
	std::vector<std::string> features;
	uint32_t input_channels = 0;
	uint32_t output_channels = 0;
	std::vector<PluginParameterInfo> parameters;
};

class ClapExternalPluginHost {
public:
	ClapExternalPluginHost();
	~ClapExternalPluginHost();

	bool Initialize(double sample_rate, uint32_t max_block_size);
	bool Load(const std::string& plugin_reference);
	bool Process(const float* input, float* output, uint32_t frames);
	bool SetParameter(uint32_t id, float value);
	bool GetParameter(uint32_t id, float* value) const;
	std::vector<PluginParameterInfo> GetParameters() const;
	ClapPluginMetadata GetMetadataPluginClap() const;
	std::string GetLoadedIdentifier() const;

private:
	struct SingleEventList {
		const ClapAbi::clap_event_header* event = nullptr;
	};

	static const void* HostGetExtension(const ClapAbi::clap_host* host, const char* extension_id);
	static void HostRequestRestart(const ClapAbi::clap_host* host);
	static void HostRequestProcess(const ClapAbi::clap_host* host);
	static void HostRequestCallback(const ClapAbi::clap_host* host);
	static uint32_t InputEventsSize(const ClapAbi::clap_input_events* list);
	static const ClapAbi::clap_event_header* InputEventsGet(const ClapAbi::clap_input_events* list, uint32_t index);
	static bool OutputEventsTryPush(const ClapAbi::clap_output_events* list, const ClapAbi::clap_event_header* event);

	void Reset();
	bool SyncParameterSnapshot();
	bool ConfigureAudioPorts();
	void CaptureLoadedMetadata(const ClapAbi::clap_plugin_descriptor* descriptor, const std::string& plugin_path);
	bool SplitPluginReference(const std::string& reference, std::string* path_out, std::string* plugin_id_out) const;

	void* library_handle_ = nullptr;
	const ClapAbi::clap_plugin_entry* entry_ = nullptr;
	const ClapAbi::clap_plugin_factory* factory_ = nullptr;
	const ClapAbi::clap_plugin* plugin_ = nullptr;
	const ClapAbi::clap_plugin_params* params_extension_ = nullptr;
	const ClapAbi::clap_plugin_param_metadata* param_metadata_extension_ = nullptr;
	const ClapAbi::clap_plugin_audio_ports* audio_ports_extension_ = nullptr;
	ClapAbi::clap_host host_{};
	PluginParameterBridge parameter_bridge_;
	double sample_rate_ = 44100.0;
	uint32_t max_block_size_ = 512;
	uint32_t input_channels_ = 1;
	uint32_t output_channels_ = 1;
	std::string loaded_identifier_;
	ClapPluginMetadata loaded_metadata_;
};

}  // namespace XER::Engine::Audio::Plugin