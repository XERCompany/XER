#include "audio/plugin_wrappers/clap_minimal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

using namespace XER::Audio::Plugin::ClapAbi;

constexpr clap_id kGainParameterId = 0u;

float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}

struct GainPluginState {
	clap_plugin plugin{};
	const clap_host* host = nullptr;
	double sample_rate = 44100.0;
	uint32_t max_frames = 512u;
	float gain_db = 0.0f;
};

const char* const kPluginFeatures[] = {
	CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
	CLAP_PLUGIN_FEATURE_UTILITY,
	CLAP_PLUGIN_FEATURE_MONO,
	nullptr,
};

const clap_plugin_descriptor kPluginDescriptor{
	CLAP_VERSION,
	"engine.xer.clap.smoke-gain",
	"XER Smoke Gain",
	"XER",
	"https://xer.local",
	"",
	"",
	"1.0",
	"Minimal gain plugin used to validate external CLAP hosting.",
	kPluginFeatures,
};

GainPluginState* GetState(const clap_plugin* plugin) {
	return plugin != nullptr ? static_cast<GainPluginState*>(plugin->plugin_data) : nullptr;
}

bool ENGINE_CLAP_ABI PluginInit(const clap_plugin* plugin) {
	return GetState(plugin) != nullptr;
}

void ENGINE_CLAP_ABI PluginDestroy(const clap_plugin* plugin) {
	delete GetState(plugin);
}

bool ENGINE_CLAP_ABI PluginActivate(const clap_plugin* plugin, double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) {
	(void)min_frames_count;
	GainPluginState* state = GetState(plugin);
	if (state == nullptr || sample_rate <= 0.0 || max_frames_count == 0) {
		return false;
	}
	state->sample_rate = sample_rate;
	state->max_frames = max_frames_count;
	return true;
}

void ENGINE_CLAP_ABI PluginDeactivate(const clap_plugin* plugin) {
	(void)plugin;
}

bool ENGINE_CLAP_ABI PluginStartProcessing(const clap_plugin* plugin) {
	return GetState(plugin) != nullptr;
}

void ENGINE_CLAP_ABI PluginStopProcessing(const clap_plugin* plugin) {
	(void)plugin;
}

void ENGINE_CLAP_ABI PluginReset(const clap_plugin* plugin) {
	(void)plugin;
}

void ApplyParameterEvents(GainPluginState* state, const clap_input_events* events) {
	if (state == nullptr || events == nullptr || events->size == nullptr || events->get == nullptr) {
		return;
	}
	const uint32_t event_count = events->size(events);
	for (uint32_t index = 0; index < event_count; ++index) {
		const clap_event_header* header = events->get(events, index);
		if (header == nullptr || header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE || header->size < sizeof(clap_event_param_value)) {
			continue;
		}
		const auto* event = reinterpret_cast<const clap_event_param_value*>(header);
		if (event->param_id == kGainParameterId) {
			state->gain_db = static_cast<float>(std::clamp(event->value, -24.0, 24.0));
		}
	}
}

clap_process_status ENGINE_CLAP_ABI PluginProcess(const clap_plugin* plugin, const clap_process* process) {
	GainPluginState* state = GetState(plugin);
	if (state == nullptr || process == nullptr || process->audio_inputs == nullptr || process->audio_outputs == nullptr || process->audio_inputs_count == 0 || process->audio_outputs_count == 0) {
		return CLAP_PROCESS_ERROR;
	}
	ApplyParameterEvents(state, process->in_events);
	if (process->audio_inputs[0].channel_count == 0 || process->audio_outputs[0].channel_count == 0 || process->audio_inputs[0].data32 == nullptr || process->audio_outputs[0].data32 == nullptr) {
		return CLAP_PROCESS_ERROR;
	}
	const float* input = process->audio_inputs[0].data32[0];
	float* output = process->audio_outputs[0].data32[0];
	if (input == nullptr || output == nullptr) {
		return CLAP_PROCESS_ERROR;
	}
	const float linear = DbToLinear(state->gain_db);
	for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
		output[frame] = input[frame] * linear;
	}
	return CLAP_PROCESS_CONTINUE;
}

uint32_t ENGINE_CLAP_ABI ParamsCount(const clap_plugin* plugin) {
	return GetState(plugin) != nullptr ? 1u : 0u;
}

bool ENGINE_CLAP_ABI ParamsGetInfo(const clap_plugin* plugin, uint32_t param_index, clap_param_info* param_info) {
	if (GetState(plugin) == nullptr || param_info == nullptr || param_index != 0u) {
		return false;
	}
	*param_info = {};
	param_info->id = kGainParameterId;
	param_info->flags = CLAP_PARAM_IS_AUTOMATABLE;
	std::snprintf(param_info->name, CLAP_NAME_SIZE, "%s", "gain_db");
	std::snprintf(param_info->module, CLAP_PATH_SIZE, "%s", "Main");
	param_info->min_value = -24.0;
	param_info->max_value = 24.0;
	param_info->default_value = 0.0;
	return true;
}

bool ENGINE_CLAP_ABI ParamsGetValue(const clap_plugin* plugin, clap_id param_id, double* out_value) {
	GainPluginState* state = GetState(plugin);
	if (state == nullptr || out_value == nullptr || param_id != kGainParameterId) {
		return false;
	}
	*out_value = state->gain_db;
	return true;
}

bool ENGINE_CLAP_ABI ParamsValueToText(const clap_plugin* plugin, clap_id param_id, double value, char* out_buffer, uint32_t out_buffer_capacity) {
	(void)plugin;
	if (param_id != kGainParameterId || out_buffer == nullptr || out_buffer_capacity == 0) {
		return false;
	}
	std::snprintf(out_buffer, out_buffer_capacity, "%.2f dB", value);
	return true;
}

bool ENGINE_CLAP_ABI ParamsTextToValue(const clap_plugin* plugin, clap_id param_id, const char* param_value_text, double* out_value) {
	(void)plugin;
	if (param_id != kGainParameterId || param_value_text == nullptr || out_value == nullptr) {
		return false;
	}
	char* end = nullptr;
	const double parsed = std::strtod(param_value_text, &end);
	if (end == param_value_text) {
		return false;
	}
	*out_value = parsed;
	return true;
}

void ENGINE_CLAP_ABI ParamsFlush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out) {
	(void)out;
	ApplyParameterEvents(GetState(plugin), in);
}

const clap_plugin_params kParamsExtension{
	&ParamsCount,
	&ParamsGetInfo,
	&ParamsGetValue,
	&ParamsValueToText,
	&ParamsTextToValue,
	&ParamsFlush,
};

uint32_t ENGINE_CLAP_ABI AudioPortsCount(const clap_plugin* plugin, bool is_input) {
	(void)is_input;
	return GetState(plugin) != nullptr ? 1u : 0u;
}

bool ENGINE_CLAP_ABI AudioPortsGet(const clap_plugin* plugin, uint32_t index, bool is_input, clap_audio_port_info* info) {
	if (GetState(plugin) == nullptr || info == nullptr || index != 0u) {
		return false;
	}
	*info = {};
	info->id = is_input ? 0u : 1u;
	std::snprintf(info->name, CLAP_NAME_SIZE, "%s", is_input ? "Input" : "Output");
	info->flags = CLAP_AUDIO_PORT_IS_MAIN;
	info->channel_count = 1u;
	info->port_type = CLAP_PORT_MONO;
	info->in_place_pair = is_input ? 1u : 0u;
	return true;
}

const clap_plugin_audio_ports kAudioPortsExtension{
	&AudioPortsCount,
	&AudioPortsGet,
};

const void* ENGINE_CLAP_ABI PluginGetExtension(const clap_plugin* plugin, const char* id) {
	if (GetState(plugin) == nullptr || id == nullptr) {
		return nullptr;
	}
	if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
		return &kParamsExtension;
	}
	if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) {
		return &kAudioPortsExtension;
	}
	return nullptr;
}

void ENGINE_CLAP_ABI PluginOnMainThread(const clap_plugin* plugin) {
	(void)plugin;
}

const clap_plugin* ENGINE_CLAP_ABI FactoryCreatePlugin(const clap_plugin_factory* factory, const clap_host* host, const char* plugin_id) {
	(void)factory;
	if (host == nullptr || plugin_id == nullptr || std::strcmp(plugin_id, kPluginDescriptor.id) != 0) {
		return nullptr;
	}
	auto* state = new GainPluginState();
	state->host = host;
	state->plugin.desc = &kPluginDescriptor;
	state->plugin.plugin_data = state;
	state->plugin.init = &PluginInit;
	state->plugin.destroy = &PluginDestroy;
	state->plugin.activate = &PluginActivate;
	state->plugin.deactivate = &PluginDeactivate;
	state->plugin.start_processing = &PluginStartProcessing;
	state->plugin.stop_processing = &PluginStopProcessing;
	state->plugin.reset = &PluginReset;
	state->plugin.process = &PluginProcess;
	state->plugin.get_extension = &PluginGetExtension;
	state->plugin.on_main_thread = &PluginOnMainThread;
	return &state->plugin;
}

uint32_t ENGINE_CLAP_ABI FactoryGetPluginCount(const clap_plugin_factory* factory) {
	(void)factory;
	return 1u;
}

const clap_plugin_descriptor* ENGINE_CLAP_ABI FactoryGetPluginDescriptor(const clap_plugin_factory* factory, uint32_t index) {
	(void)factory;
	return index == 0u ? &kPluginDescriptor : nullptr;
}

const clap_plugin_factory kPluginFactory{
	&FactoryGetPluginCount,
	&FactoryGetPluginDescriptor,
	&FactoryCreatePlugin,
};

bool ENGINE_CLAP_ABI EntryInit(const char* plugin_path) {
	(void)plugin_path;
	return true;
}

void ENGINE_CLAP_ABI EntryDeinit() {}

const void* ENGINE_CLAP_ABI EntryGetFactory(const char* factory_id) {
	if (factory_id != nullptr && std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
		return &kPluginFactory;
	}
	return nullptr;
}

}  // namespace

extern "C" ENGINE_CLAP_EXPORT const XER::Audio::Plugin::ClapAbi::clap_plugin_entry clap_entry = {
	XER::Audio::Plugin::ClapAbi::CLAP_VERSION,
	&EntryInit,
	&EntryDeinit,
	&EntryGetFactory,
};