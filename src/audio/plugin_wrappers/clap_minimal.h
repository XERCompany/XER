#pragma once

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>

#if defined(_WIN32)
#define ENGINE_CLAP_ABI __cdecl
#define ENGINE_CLAP_EXPORT __declspec(dllexport)
#else
#define ENGINE_CLAP_ABI
#define ENGINE_CLAP_EXPORT __attribute__((visibility("default")))
#endif

namespace XER::Engine::Audio::Plugin::ClapAbi {

struct clap_version_t {
	uint32_t major;
	uint32_t minor;
	uint32_t revision;
};

inline constexpr clap_version_t CLAP_VERSION{1u, 2u, 7u};

inline bool clap_version_is_compatible(const clap_version_t& version) {
	return version.major >= 1u;
}

using clap_id = uint32_t;
inline constexpr clap_id CLAP_INVALID_ID = 0xFFFFFFFFu;

enum {
	CLAP_NAME_SIZE = 256,
	CLAP_PATH_SIZE = 1024,
};

struct clap_host;
struct clap_plugin;
struct clap_plugin_descriptor;
struct clap_input_events;
struct clap_output_events;
struct clap_event_transport;

using clap_process_status = int32_t;

enum {
	CLAP_PROCESS_ERROR = 0,
	CLAP_PROCESS_CONTINUE = 1,
	CLAP_PROCESS_CONTINUE_IF_NOT_QUIET = 2,
	CLAP_PROCESS_TAIL = 3,
	CLAP_PROCESS_SLEEP = 4,
};

struct clap_audio_buffer {
	float** data32;
	double** data64;
	uint32_t channel_count;
	uint32_t latency;
	uint64_t constant_mask;
};

struct clap_event_header {
	uint32_t size;
	uint32_t time;
	uint16_t space_id;
	uint16_t type;
	uint32_t flags;
};

inline constexpr uint16_t CLAP_CORE_EVENT_SPACE_ID = 0;
inline constexpr uint16_t CLAP_EVENT_PARAM_VALUE = 5;

struct clap_event_param_value {
	clap_event_header header;
	clap_id param_id;
	void* cookie;
	int32_t note_id;
	int16_t port_index;
	int16_t channel;
	int16_t key;
	double value;
};

struct clap_input_events {
	void* ctx;
	uint32_t(ENGINE_CLAP_ABI* size)(const clap_input_events* list);
	const clap_event_header*(ENGINE_CLAP_ABI* get)(const clap_input_events* list, uint32_t index);
};

struct clap_output_events {
	void* ctx;
	bool(ENGINE_CLAP_ABI* try_push)(const clap_output_events* list, const clap_event_header* event);
};

struct clap_process {
	int64_t steady_time;
	uint32_t frames_count;
	const clap_event_transport* transport;
	const clap_audio_buffer* audio_inputs;
	clap_audio_buffer* audio_outputs;
	uint32_t audio_inputs_count;
	uint32_t audio_outputs_count;
	const clap_input_events* in_events;
	const clap_output_events* out_events;
};

struct clap_host {
	clap_version_t clap_version;
	void* host_data;
	const char* name;
	const char* vendor;
	const char* url;
	const char* version;
	const void*(ENGINE_CLAP_ABI* get_extension)(const clap_host* host, const char* extension_id);
	void(ENGINE_CLAP_ABI* request_restart)(const clap_host* host);
	void(ENGINE_CLAP_ABI* request_process)(const clap_host* host);
	void(ENGINE_CLAP_ABI* request_callback)(const clap_host* host);
};

struct clap_plugin_descriptor {
	clap_version_t clap_version;
	const char* id;
	const char* name;
	const char* vendor;
	const char* url;
	const char* manual_url;
	const char* support_url;
	const char* version;
	const char* description;
	const char* const* features;
};

struct clap_plugin {
	const clap_plugin_descriptor* desc;
	void* plugin_data;
	bool(ENGINE_CLAP_ABI* init)(const clap_plugin* plugin);
	void(ENGINE_CLAP_ABI* destroy)(const clap_plugin* plugin);
	bool(ENGINE_CLAP_ABI* activate)(const clap_plugin* plugin, double sample_rate, uint32_t min_frames_count, uint32_t max_frames_count);
	void(ENGINE_CLAP_ABI* deactivate)(const clap_plugin* plugin);
	bool(ENGINE_CLAP_ABI* start_processing)(const clap_plugin* plugin);
	void(ENGINE_CLAP_ABI* stop_processing)(const clap_plugin* plugin);
	void(ENGINE_CLAP_ABI* reset)(const clap_plugin* plugin);
	clap_process_status(ENGINE_CLAP_ABI* process)(const clap_plugin* plugin, const clap_process* process);
	const void*(ENGINE_CLAP_ABI* get_extension)(const clap_plugin* plugin, const char* id);
	void(ENGINE_CLAP_ABI* on_main_thread)(const clap_plugin* plugin);
};

inline constexpr char CLAP_PLUGIN_FACTORY_ID[] = "clap.plugin-factory";

struct clap_plugin_factory {
	uint32_t(ENGINE_CLAP_ABI* get_plugin_count)(const clap_plugin_factory* factory);
	const clap_plugin_descriptor*(ENGINE_CLAP_ABI* get_plugin_descriptor)(const clap_plugin_factory* factory, uint32_t index);
	const clap_plugin*(ENGINE_CLAP_ABI* create_plugin)(const clap_plugin_factory* factory, const clap_host* host, const char* plugin_id);
};

struct clap_plugin_entry {
	clap_version_t clap_version;
	bool(ENGINE_CLAP_ABI* init)(const char* plugin_path);
	void(ENGINE_CLAP_ABI* deinit)();
	const void*(ENGINE_CLAP_ABI* get_factory)(const char* factory_id);
};

inline constexpr char CLAP_EXT_PARAMS[] = "clap.params";

enum {
	CLAP_PARAM_IS_STEPPED = 1 << 0,
	CLAP_PARAM_IS_AUTOMATABLE = 1 << 5,
};

using clap_param_info_flags = uint32_t;

struct clap_param_info {
	clap_id id;
	clap_param_info_flags flags;
	void* cookie;
	char name[CLAP_NAME_SIZE];
	char module[CLAP_PATH_SIZE];
	double min_value;
	double max_value;
	double default_value;
};

struct clap_plugin_params {
	uint32_t(ENGINE_CLAP_ABI* count)(const clap_plugin* plugin);
	bool(ENGINE_CLAP_ABI* get_info)(const clap_plugin* plugin, uint32_t param_index, clap_param_info* param_info);
	bool(ENGINE_CLAP_ABI* get_value)(const clap_plugin* plugin, clap_id param_id, double* out_value);
	bool(ENGINE_CLAP_ABI* value_to_text)(const clap_plugin* plugin, clap_id param_id, double value, char* out_buffer, uint32_t out_buffer_capacity);
	bool(ENGINE_CLAP_ABI* text_to_value)(const clap_plugin* plugin, clap_id param_id, const char* param_value_text, double* out_value);
	void(ENGINE_CLAP_ABI* flush)(const clap_plugin* plugin, const clap_input_events* in, const clap_output_events* out);
};

inline constexpr char CLAP_EXT_AUDIO_PORTS[] = "clap.audio-ports";
inline constexpr char CLAP_PORT_MONO[] = "mono";
inline constexpr char CLAP_PORT_STEREO[] = "stereo";

enum {
	CLAP_AUDIO_PORT_IS_MAIN = 1 << 0,
};

struct clap_audio_port_info {
	clap_id id;
	char name[CLAP_NAME_SIZE];
	uint32_t flags;
	uint32_t channel_count;
	const char* port_type;
	clap_id in_place_pair;
};

struct clap_plugin_audio_ports {
	uint32_t(ENGINE_CLAP_ABI* count)(const clap_plugin* plugin, bool is_input);
	bool(ENGINE_CLAP_ABI* get)(const clap_plugin* plugin, uint32_t index, bool is_input, clap_audio_port_info* info);
};

inline constexpr char CLAP_PLUGIN_FEATURE_AUDIO_EFFECT[] = "audio-effect";
inline constexpr char CLAP_PLUGIN_FEATURE_UTILITY[] = "utility";
inline constexpr char CLAP_PLUGIN_FEATURE_MONO[] = "mono";
inline constexpr char CLAP_PLUGIN_FEATURE_STEREO[] = "stereo";
inline constexpr char CLAP_PLUGIN_FEATURE_EQUALIZER[] = "equalizer";
inline constexpr char CLAP_PLUGIN_FEATURE_FILTER[] = "filter";
inline constexpr char CLAP_PLUGIN_FEATURE_PITCH_SHIFTER[] = "pitch-shifter";

struct clap_minimal_param_descriptor {
	clap_id id;
	const char* name;
	const char* module;
	double min_value;
	double max_value;
	double default_value;
	clap_param_info_flags flags;
	const char* unit;
	const char* ui_hint;
	const char* description;
};

inline constexpr char ENGINE_CLAP_EXT_PARAM_METADATA[] = "engine.xer.clap.param-metadata/1";
inline constexpr char ENGINE_CLAP_PARAM_UI_INPUT[] = "input";
inline constexpr char ENGINE_CLAP_PARAM_UI_SLIDER[] = "slider";
inline constexpr char ENGINE_CLAP_PARAM_UI_TOGGLE[] = "toggle";
inline constexpr char ENGINE_CLAP_PARAM_UI_MENU[] = "menu";

struct clap_param_metadata_info {
	clap_id id;
	char unit[CLAP_NAME_SIZE];
	char ui_hint[CLAP_NAME_SIZE];
	char group[CLAP_PATH_SIZE];
	char description[CLAP_PATH_SIZE];
};

struct clap_plugin_param_metadata {
	bool(ENGINE_CLAP_ABI* get)(const clap_plugin* plugin, clap_id param_id, clap_param_metadata_info* metadata_info);
};

inline void clap_copy_fixed_string(char* target, uint32_t capacity, const char* source) {
	if (target == nullptr || capacity == 0) {
		return;
	}
	std::snprintf(target, capacity, "%s", source != nullptr ? source : "");
}

inline bool clap_fill_param_info(const clap_minimal_param_descriptor& descriptor, clap_param_info* param_info) {
	if (param_info == nullptr) {
		return false;
	}
	*param_info = {};
	param_info->id = descriptor.id;
	param_info->flags = descriptor.flags;
	clap_copy_fixed_string(param_info->name, CLAP_NAME_SIZE, descriptor.name);
	clap_copy_fixed_string(param_info->module, CLAP_PATH_SIZE, descriptor.module);
	param_info->min_value = descriptor.min_value;
	param_info->max_value = descriptor.max_value;
	param_info->default_value = descriptor.default_value;
	return true;
}

inline bool clap_fill_param_metadata(const clap_minimal_param_descriptor& descriptor, clap_param_metadata_info* metadata_info) {
	if (metadata_info == nullptr) {
		return false;
	}
	*metadata_info = {};
	metadata_info->id = descriptor.id;
	clap_copy_fixed_string(metadata_info->unit, CLAP_NAME_SIZE, descriptor.unit);
	clap_copy_fixed_string(metadata_info->ui_hint, CLAP_NAME_SIZE, descriptor.ui_hint != nullptr ? descriptor.ui_hint : ENGINE_CLAP_PARAM_UI_SLIDER);
	clap_copy_fixed_string(metadata_info->group, CLAP_PATH_SIZE, descriptor.module);
	clap_copy_fixed_string(metadata_info->description, CLAP_PATH_SIZE, descriptor.description);
	return true;
}

inline int clap_find_param_index(const clap_minimal_param_descriptor* descriptors, uint32_t descriptor_count, clap_id id) {
	if (descriptors == nullptr) {
		return -1;
	}
	for (uint32_t index = 0; index < descriptor_count; ++index) {
		if (descriptors[index].id == id) {
			return static_cast<int>(index);
		}
	}
	return -1;
}

inline double clap_clamp_param_value(const clap_minimal_param_descriptor& descriptor, double value) {
	return std::clamp(value, descriptor.min_value, descriptor.max_value);
}

}  // namespace XER::Engine::Audio::Plugin::ClapAbi