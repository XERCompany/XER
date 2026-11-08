#include "audio/plugin_wrappers/clap_minimal.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using namespace XER::Audio::Plugin::ClapAbi;

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kParameterCount = 5u;

enum ParameterId : clap_id {
	kSemitones = 0u,
	kFineCents = 1u,
	kMix = 2u,
	kWindowMs = 3u,
	kOutputGainDb = 4u,
};

constexpr clap_minimal_param_descriptor kParameterDescriptors[kParameterCount] = {
	{kSemitones, "pitch_semitones", "Pitch", -12.0, 12.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE, "st", ENGINE_CLAP_PARAM_UI_SLIDER, "Pitch offset in semitones."},
	{kFineCents, "fine_cents", "Pitch", -100.0, 100.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE, "cent", ENGINE_CLAP_PARAM_UI_SLIDER, "Fine pitch offset in cents."},
	{kMix, "mix", "Pitch", 0.0, 1.0, 1.0, CLAP_PARAM_IS_AUTOMATABLE, "", ENGINE_CLAP_PARAM_UI_SLIDER, "Blend between dry input and shifted output."},
	{kWindowMs, "window_ms", "Pitch", 20.0, 120.0, 45.0, CLAP_PARAM_IS_AUTOMATABLE, "ms", ENGINE_CLAP_PARAM_UI_SLIDER, "Granular delay window used by the time-domain shifter."},
	{kOutputGainDb, "output_gain_db", "Output", -18.0, 18.0, 0.0, CLAP_PARAM_IS_AUTOMATABLE, "dB", ENGINE_CLAP_PARAM_UI_SLIDER, "Final output trim after pitch shifting."},
};

const char* const kPluginFeatures[] = {
	CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
	CLAP_PLUGIN_FEATURE_PITCH_SHIFTER,
	CLAP_PLUGIN_FEATURE_MONO,
	nullptr,
};

const clap_plugin_descriptor kPluginDescriptor{
	CLAP_VERSION,
	"engine.xer.clap.pitch-shifter",
	"XER PitchShifter",
	"XER",
	"https://xer.local",
	"",
	"",
	"1.0.0",
	"Standalone mono time-domain pitch shifter CLAP plugin with semitone, fine cents, mix, window, and output trim controls.",
	kPluginFeatures,
};

double DbToLinear(double db_value) {
	return std::pow(10.0, db_value / 20.0);
}

struct PitchShifterPluginState {
	clap_plugin plugin{};
	const clap_host* host = nullptr;
	double sample_rate = 44100.0;
	uint32_t max_frames = 512u;
	double values[kParameterCount]{};
	std::vector<float> delay_buffer;
	size_t write_index = 0u;
	double phase = 0.0;

	PitchShifterPluginState() {
		for (uint32_t index = 0; index < kParameterCount; ++index) {
			values[index] = kParameterDescriptors[index].default_value;
		}
	}

	void AllocateDelay() {
		const size_t requested_size = static_cast<size_t>(std::ceil(sample_rate * 0.35)) + max_frames + 8u;
		delay_buffer.assign(std::max<size_t>(requested_size, 4096u), 0.0f);
		write_index = 0u;
		phase = 0.0;
	}

	float ReadDelayed(double delay_samples) const {
		if (delay_buffer.empty()) {
			return 0.0f;
		}
		const double buffer_size = static_cast<double>(delay_buffer.size());
		double position = static_cast<double>(write_index) - delay_samples;
		while (position < 0.0) {
			position += buffer_size;
		}
		while (position >= buffer_size) {
			position -= buffer_size;
		}
		const auto index0 = static_cast<size_t>(position) % delay_buffer.size();
		const size_t index1 = (index0 + 1u) % delay_buffer.size();
		const double fraction = position - std::floor(position);
		return static_cast<float>(delay_buffer[index0] + (delay_buffer[index1] - delay_buffer[index0]) * fraction);
	}
};

PitchShifterPluginState* GetState(const clap_plugin* plugin) {
	return plugin != nullptr ? static_cast<PitchShifterPluginState*>(plugin->plugin_data) : nullptr;
}

void ApplyParameterEvents(PitchShifterPluginState* state, const clap_input_events* events) {
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
	PitchShifterPluginState* state = GetState(plugin);
	if (state == nullptr || sample_rate <= 0.0 || max_frames_count == 0u) {
		return false;
	}
	state->sample_rate = sample_rate;
	state->max_frames = max_frames_count;
	state->AllocateDelay();
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
	PitchShifterPluginState* state = GetState(plugin);
	if (state != nullptr) {
		std::fill(state->delay_buffer.begin(), state->delay_buffer.end(), 0.0f);
		state->write_index = 0u;
		state->phase = 0.0;
	}
}

clap_process_status ENGINE_CLAP_ABI PluginProcess(const clap_plugin* plugin, const clap_process* process) {
	PitchShifterPluginState* state = GetState(plugin);
	if (state == nullptr || process == nullptr || process->audio_inputs == nullptr || process->audio_outputs == nullptr || process->audio_inputs_count == 0u || process->audio_outputs_count == 0u || state->delay_buffer.empty()) {
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

	const double semitones = state->values[kSemitones] + state->values[kFineCents] / 100.0;
	const double pitch_ratio = std::pow(2.0, semitones / 12.0);
	const double mix = state->values[kMix];
	const double output_gain = DbToLinear(state->values[kOutputGainDb]);
	const double window_samples = std::clamp(state->values[kWindowMs] * state->sample_rate / 1000.0, 32.0, static_cast<double>(state->delay_buffer.size() - 4u));
	const double phase_step = (pitch_ratio - 1.0) / window_samples;

	for (uint32_t frame = 0; frame < process->frames_count; ++frame) {
		const float dry = input[frame];
		state->delay_buffer[state->write_index] = dry;

		float shifted = dry;
		if (std::abs(semitones) > 0.001) {
			state->phase += phase_step;
			while (state->phase >= 1.0) {
				state->phase -= 1.0;
			}
			while (state->phase < 0.0) {
				state->phase += 1.0;
			}

			const double second_phase = state->phase < 0.5 ? state->phase + 0.5 : state->phase - 0.5;
			const double first_delay = pitch_ratio >= 1.0 ? (1.0 - state->phase) * window_samples : state->phase * window_samples;
			const double second_delay = pitch_ratio >= 1.0 ? (1.0 - second_phase) * window_samples : second_phase * window_samples;
			const double fade = 0.5 - 0.5 * std::cos(2.0 * kPi * state->phase);
			const float first = state->ReadDelayed(first_delay);
			const float second = state->ReadDelayed(second_delay);
			shifted = static_cast<float>(first * (1.0 - fade) + second * fade);
		}

		output[frame] = static_cast<float>((dry + (shifted - dry) * mix) * output_gain);
		state->write_index = (state->write_index + 1u) % state->delay_buffer.size();
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
	PitchShifterPluginState* state = GetState(plugin);
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
	auto* state = new PitchShifterPluginState();
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