#include "audio/plugin_wrappers/clap_minimal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

using namespace XER::Audio::Plugin::ClapAbi;

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kParameterCount = 3u;

enum ParameterId : clap_id {
	kBoostDb = 0u,
	kFrequencyHz = 1u,
	kMix = 2u,
};

constexpr clap_minimal_param_descriptor kParameterDescriptors[kParameterCount] = {
	{kBoostDb, "boost_db", "BassBoost", 0.0, 18.0, 6.0, CLAP_PARAM_IS_AUTOMATABLE, "dB", ENGINE_CLAP_PARAM_UI_SLIDER, "Amount of low-band enhancement added to the dry signal."},
	{kFrequencyHz, "frequency_hz", "BassBoost", 35.0, 260.0, 110.0, CLAP_PARAM_IS_AUTOMATABLE, "Hz", ENGINE_CLAP_PARAM_UI_SLIDER, "Lowpass cutoff that defines the enhanced bass band."},
	{kMix, "mix", "Output", 0.0, 1.0, 0.75, CLAP_PARAM_IS_AUTOMATABLE, "", ENGINE_CLAP_PARAM_UI_SLIDER, "Blend between dry input and boosted bass output."},
};

const char* const kPluginFeatures[] = {
	CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
	CLAP_PLUGIN_FEATURE_FILTER,
	CLAP_PLUGIN_FEATURE_MONO,
	nullptr,
};

const clap_plugin_descriptor kPluginDescriptor{
	CLAP_VERSION,
	"engine.xer.clap.bass-boost",
	"XER BassBoost",
	"XER",
	"https://xer.local",
	"",
	"",
	"1.0.0",
	"Standalone mono bass enhancer CLAP plugin with low-band boost, cutoff, and mix controls.",
	kPluginFeatures,
};

double DbToLinear(double db_value) {
	return std::pow(10.0, db_value / 20.0);
}

struct BassBoostPluginState {
	clap_plugin plugin{};
	const clap_host* host = nullptr;
	double sample_rate = 44100.0;
	uint32_t max_frames = 512u;
	double values[kParameterCount]{};
	double low_state = 0.0;

	BassBoostPluginState() {
		for (uint32_t index = 0; index < kParameterCount; ++index) {
			values[index] = kParameterDescriptors[index].default_value;
		}
	}

	double LowpassCoefficient() const {
		const double cutoff = std::clamp(values[kFrequencyHz], 35.0, sample_rate * 0.45);
		return 1.0 - std::exp(-2.0 * kPi * cutoff / sample_rate);
	}
};

BassBoostPluginState* GetState(const clap_plugin* plugin) {
	return plugin != nullptr ? static_cast<BassBoostPluginState*>(plugin->plugin_data) : nullptr;
}

void ApplyParameterEvents(BassBoostPluginState* state, const clap_input_events* events) {
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
		const int parameter_index = clap_find_param_index(kParameterDescriptors, kParameterCount, event->param_id);
		if (parameter_index >= 0) {
			state->values[parameter_index] = clap_clamp_param_value(kParameterDescriptors[parameter_index], event->value);
		}
	}
}

bool ENGINE_CLAP_ABI PluginInit(const clap_plugin* plugin) {
	return GetState(plugin) != nullptr;
}

void ENGINE_CLAP_ABI PluginDestroy(const clap_plugin* plugin) {
	delete GetState(plugin);
}

bool ENGINE_CLAP_ABI PluginActivate(const clap_plugin* plugin, double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count) {
	(void)min_frames_count;
	BassBoostPluginState* state = GetState(plugin);
	if (state == nullptr || sample_rate <= 0.0 || max_frames_count == 0u) {
		return false;
	}
	state->sample_rate = sample_rate;
	state->max_frames = max_frames_count;
	state->low_state = 0.0;
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
	BassBoostPluginState* state = GetState(plugin);
	if (state != nullptr) {
		state->low_state = 0.0;
	}
}

clap_process_status ENGINE_CLAP_ABI PluginProcess(const clap_plugin* plugin, const clap_process* process) {
	BassBoostPluginState* state = GetState(plugin);
	if (state == nullptr || process == nullptr || process->audio_inputs == nullptr || process->audio_outputs == nullptr || process->audio_inputs_count == 0u || process->audio_outputs_count == 0u) {
		return CLAP_PROCESS_ERROR;
	}
	ApplyParameterEvents(state, process->in_events);
	if (process->frames_count > state->max_frames || process->audio_inputs[0].channel_count == 0u || process->audio_outputs[0].channel_count == 0u || process->audio_inputs[0].data32 == nullptr || process->audio_outputs[0].data32 == nullptr) {
		return CLAP_PROCESS_ERROR;
	}
	const float* input = process->audio_inputs[0].data32[0];
	float* output = process->audio_outputs[0].data32[0];
	if (input == nullptr || output == nullptr) {
		return CLAP_PROCESS_ERROR;
	}
	const double coefficient = state->LowpassCoefficient();
	const double boost_amount = DbToLinear(state->values[kBoostDb]) - 1.0;
	const double mix = state->values[kMix];
	for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
		const double dry = input[frame];
		state->low_state += coefficient * (dry - state->low_state);
		const double boosted = dry + state->low_state * boost_amount;
		output[frame] = static_cast<float>(dry + (boosted - dry) * mix);
	}
	return CLAP_PROCESS_CONTINUE;
}

uint32_t ENGINE_CLAP_ABI ParamsCount(const clap_plugin* plugin) {
	return GetState(plugin) != nullptr ? kParameterCount : 0u;
}

bool ENGINE_CLAP_ABI ParamsGetInfo(const clap_plugin* plugin, uint32_t param_index, clap_param_info* param_info) {
	return GetState(plugin) != nullptr && param_index < kParameterCount && clap_fill_param_info(kParameterDescriptors[param_index], param_info);
}

bool ENGINE_CLAP_ABI ParamsGetValue(const clap_plugin* plugin, clap_id param_id, double* out_value) {
	BassBoostPluginState* state = GetState(plugin);
	const int parameter_index = clap_find_param_index(kParameterDescriptors, kParameterCount, param_id);
	if (state == nullptr || out_value == nullptr || parameter_index < 0) {
		return false;
	}
	*out_value = state->values[parameter_index];
	return true;
}

bool ENGINE_CLAP_ABI ParamsValueToText(const clap_plugin* plugin, clap_id param_id, double value, char* out_buffer, uint32_t out_buffer_capacity) {
	(void)plugin;
	const int parameter_index = clap_find_param_index(kParameterDescriptors, kParameterCount, param_id);
	if (parameter_index < 0 || out_buffer == nullptr || out_buffer_capacity == 0u) {
		return false;
	}
	std::snprintf(out_buffer, out_buffer_capacity, "%.3f%s%s", value, kParameterDescriptors[parameter_index].unit[0] != '\0' ? " " : "", kParameterDescriptors[parameter_index].unit);
	return true;
}

bool ENGINE_CLAP_ABI ParamsTextToValue(const clap_plugin* plugin, clap_id param_id, const char* param_value_text, double* out_value) {
	(void)plugin;
	const int parameter_index = clap_find_param_index(kParameterDescriptors, kParameterCount, param_id);
	if (parameter_index < 0 || param_value_text == nullptr || out_value == nullptr) {
		return false;
	}
	char* end = nullptr;
	const double parsed = std::strtod(param_value_text, &end);
	if (end == param_value_text) {
		return false;
	}
	*out_value = clap_clamp_param_value(kParameterDescriptors[parameter_index], parsed);
	return true;
}

void ENGINE_CLAP_ABI ParamsFlush(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out) {
	(void)out;
	ApplyParameterEvents(GetState(plugin), in);
}

const clap_plugin_params kParamsExtension{&ParamsCount, &ParamsGetInfo, &ParamsGetValue, &ParamsValueToText, &ParamsTextToValue, &ParamsFlush};

bool ENGINE_CLAP_ABI ParamMetadataGet(const clap_plugin* plugin, clap_id param_id, clap_param_metadata_info* metadata_info) {
	const int parameter_index = clap_find_param_index(kParameterDescriptors, kParameterCount, param_id);
	return GetState(plugin) != nullptr && parameter_index >= 0 && clap_fill_param_metadata(kParameterDescriptors[parameter_index], metadata_info);
}

const clap_plugin_param_metadata kParamMetadataExtension{&ParamMetadataGet};

uint32_t ENGINE_CLAP_ABI AudioPortsCount(const clap_plugin* plugin, bool is_input) {
	(void)is_input;
	return GetState(plugin) != nullptr ? 1u : 0u;
}

bool ENGINE_CLAP_ABI AudioPortsGet(const clap_plugin* plugin, uint32_t index, bool is_input, clap_audio_port_info* info) {
	if (GetState(plugin) == nullptr || index != 0u || info == nullptr) {
		return false;
	}
	*info = {};
	info->id = is_input ? 0u : 1u;
	clap_copy_fixed_string(info->name, CLAP_NAME_SIZE, is_input ? "Input" : "Output");
	info->flags = CLAP_AUDIO_PORT_IS_MAIN;
	info->channel_count = 1u;
	info->port_type = CLAP_PORT_MONO;
	info->in_place_pair = is_input ? 1u : 0u;
	return true;
}

const clap_plugin_audio_ports kAudioPortsExtension{&AudioPortsCount, &AudioPortsGet};

const void* ENGINE_CLAP_ABI PluginGetExtension(const clap_plugin* plugin, const char* id) {
	if (GetState(plugin) == nullptr || id == nullptr) {
		return nullptr;
	}
	if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) {
		return &kParamsExtension;
	}
	if (std::strcmp(id, ENGINE_CLAP_EXT_PARAM_METADATA) == 0) {
		return &kParamMetadataExtension;
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
	auto* state = new BassBoostPluginState();
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

const clap_plugin_factory kPluginFactory{&FactoryGetPluginCount, &FactoryGetPluginDescriptor, &FactoryCreatePlugin};

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