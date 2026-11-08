#include "clap_external_plugin_host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace XER::Audio::Plugin {

namespace {

constexpr int32_t kWildcardNote = -1;

std::string MakeLoadedIdentifier(const std::string& path, const char* plugin_id) {
	if (plugin_id == nullptr || plugin_id[0] == '\0') {
		return path;
	}
	return path + "#" + plugin_id;
}

float DbToLinear(float db_value) {
	return std::pow(10.0f, db_value / 20.0f);
}

void* OpenPluginLibrary(const char* path) {
#ifdef _WIN32
	return reinterpret_cast<void*>(LoadLibraryA(path));
#else
	return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* ResolvePluginSymbol(void* handle, const char* name) {
	if (handle == nullptr) {
		return nullptr;
	}
#ifdef _WIN32
	return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
	return dlsym(handle, name);
#endif
}

void ClosePluginLibrary(void* handle) {
	if (handle == nullptr) {
		return;
	}
#ifdef _WIN32
	FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
	dlclose(handle);
#endif
}

std::string SafeClapString(const char* value) {
	return value != nullptr ? std::string(value) : std::string();
}

}  // namespace

ClapExternalPluginHost::ClapExternalPluginHost() {
	host_.clap_version = ClapAbi::CLAP_VERSION;
	host_.host_data = this;
	host_.name = "XER";
	host_.vendor = "XER";
	host_.url = "https://xer.local";
	host_.version = "1.0";
	host_.get_extension = &ClapExternalPluginHost::HostGetExtension;
	host_.request_restart = &ClapExternalPluginHost::HostRequestRestart;
	host_.request_process = &ClapExternalPluginHost::HostRequestProcess;
	host_.request_callback = &ClapExternalPluginHost::HostRequestCallback;
}

ClapExternalPluginHost::~ClapExternalPluginHost() {
	Reset();
}

bool ClapExternalPluginHost::Initialize(double sample_rate, uint32_t max_block_size) {
	if (sample_rate <= 0.0 || max_block_size == 0) {
		return false;
	}
	sample_rate_ = sample_rate;
	max_block_size_ = max_block_size;
	return true;
}

bool ClapExternalPluginHost::SplitPluginReference(const std::string& reference, std::string* path_out, std::string* plugin_id_out) const {
	if (path_out == nullptr || plugin_id_out == nullptr || reference.empty()) {
		return false;
	}
	const size_t hash = reference.find('#');
	if (hash == std::string::npos) {
		*path_out = reference;
		plugin_id_out->clear();
		return true;
	}
	*path_out = reference.substr(0, hash);
	*plugin_id_out = reference.substr(hash + 1);
	return !path_out->empty();
}

void ClapExternalPluginHost::Reset() {
	if (plugin_ != nullptr) {
		if (plugin_->stop_processing != nullptr) {
			plugin_->stop_processing(plugin_);
		}
		if (plugin_->deactivate != nullptr) {
			plugin_->deactivate(plugin_);
		}
		if (plugin_->destroy != nullptr) {
			plugin_->destroy(plugin_);
		}
	}
	plugin_ = nullptr;
	params_extension_ = nullptr;
	param_metadata_extension_ = nullptr;
	audio_ports_extension_ = nullptr;
	factory_ = nullptr;
	loaded_identifier_.clear();
	loaded_metadata_ = {};
	parameter_bridge_.Clear();
	input_channels_ = 1;
	output_channels_ = 1;
	if (entry_ != nullptr && entry_->deinit != nullptr) {
		entry_->deinit();
	}
	entry_ = nullptr;
	if (library_handle_ != nullptr) {
		ClosePluginLibrary(library_handle_);
	}
	library_handle_ = nullptr;
}

bool ClapExternalPluginHost::Load(const std::string& plugin_reference) {
	Reset();
	std::string plugin_path;
	std::string requested_plugin_id;
	if (!SplitPluginReference(plugin_reference, &plugin_path, &requested_plugin_id)) {
		return false;
	}

	library_handle_ = OpenPluginLibrary(plugin_path.c_str());
	if (library_handle_ == nullptr) {
		return false;
	}
	entry_ = reinterpret_cast<const ClapAbi::clap_plugin_entry*>(ResolvePluginSymbol(library_handle_, "clap_entry"));
	if (entry_ == nullptr || !ClapAbi::clap_version_is_compatible(entry_->clap_version) || entry_->init == nullptr || entry_->get_factory == nullptr) {
		Reset();
		return false;
	}
	if (!entry_->init(plugin_path.c_str())) {
		Reset();
		return false;
	}
	factory_ = static_cast<const ClapAbi::clap_plugin_factory*>(entry_->get_factory(ClapAbi::CLAP_PLUGIN_FACTORY_ID));
	if (factory_ == nullptr || factory_->get_plugin_count == nullptr || factory_->get_plugin_descriptor == nullptr || factory_->create_plugin == nullptr) {
		Reset();
		return false;
	}

	const ClapAbi::clap_plugin_descriptor* selected_descriptor = nullptr;
	const uint32_t plugin_count = factory_->get_plugin_count(factory_);
	for (uint32_t index = 0; index < plugin_count; ++index) {
		const ClapAbi::clap_plugin_descriptor* descriptor = factory_->get_plugin_descriptor(factory_, index);
		if (descriptor == nullptr || descriptor->id == nullptr) {
			continue;
		}
		if (requested_plugin_id.empty() || requested_plugin_id == descriptor->id) {
			selected_descriptor = descriptor;
			break;
		}
	}
	if (selected_descriptor == nullptr) {
		Reset();
		return false;
	}

	plugin_ = factory_->create_plugin(factory_, &host_, selected_descriptor->id);
	if (plugin_ == nullptr || plugin_->init == nullptr || plugin_->destroy == nullptr || plugin_->activate == nullptr || plugin_->start_processing == nullptr || plugin_->process == nullptr) {
		Reset();
		return false;
	}
	if (!plugin_->init(plugin_)) {
		Reset();
		return false;
	}
	params_extension_ = static_cast<const ClapAbi::clap_plugin_params*>(plugin_->get_extension != nullptr ? plugin_->get_extension(plugin_, ClapAbi::CLAP_EXT_PARAMS) : nullptr);
	param_metadata_extension_ = static_cast<const ClapAbi::clap_plugin_param_metadata*>(plugin_->get_extension != nullptr ? plugin_->get_extension(plugin_, ClapAbi::ENGINE_CLAP_EXT_PARAM_METADATA) : nullptr);
	audio_ports_extension_ = static_cast<const ClapAbi::clap_plugin_audio_ports*>(plugin_->get_extension != nullptr ? plugin_->get_extension(plugin_, ClapAbi::CLAP_EXT_AUDIO_PORTS) : nullptr);
	if (!ConfigureAudioPorts()) {
		Reset();
		return false;
	}
	if (!plugin_->activate(plugin_, sample_rate_, 1u, max_block_size_)) {
		Reset();
		return false;
	}
	if (!plugin_->start_processing(plugin_)) {
		Reset();
		return false;
	}
	loaded_identifier_ = MakeLoadedIdentifier(plugin_path, selected_descriptor->id);
	if (!SyncParameterSnapshot()) {
		Reset();
		return false;
	}
	CaptureLoadedMetadata(selected_descriptor, plugin_path);
	return true;
}

void ClapExternalPluginHost::CaptureLoadedMetadata(const ClapAbi::clap_plugin_descriptor* descriptor, const std::string& plugin_path) {
	loaded_metadata_ = {};
	if (descriptor == nullptr) {
		return;
	}
	loaded_metadata_.id = SafeClapString(descriptor->id);
	loaded_metadata_.title = SafeClapString(descriptor->name);
	loaded_metadata_.vendor = SafeClapString(descriptor->vendor);
	loaded_metadata_.version = SafeClapString(descriptor->version);
	loaded_metadata_.description = SafeClapString(descriptor->description);
	loaded_metadata_.plugin_path = plugin_path;
	loaded_metadata_.loaded_identifier = loaded_identifier_;
	if (descriptor->features != nullptr) {
		for (const char* const* feature = descriptor->features; *feature != nullptr; ++feature) {
			loaded_metadata_.features.emplace_back(*feature);
		}
	}
	loaded_metadata_.input_channels = input_channels_;
	loaded_metadata_.output_channels = output_channels_;
	loaded_metadata_.parameters = parameter_bridge_.Snapshot();
}

bool ClapExternalPluginHost::ConfigureAudioPorts() {
	input_channels_ = 1;
	output_channels_ = 1;
	if (audio_ports_extension_ == nullptr || audio_ports_extension_->count == nullptr || audio_ports_extension_->get == nullptr) {
		return true;
	}
	ClapAbi::clap_audio_port_info info{};
	if (audio_ports_extension_->count(plugin_, true) > 0 && audio_ports_extension_->get(plugin_, 0u, true, &info) && info.channel_count > 0) {
		input_channels_ = info.channel_count;
	}
	if (audio_ports_extension_->count(plugin_, false) > 0 && audio_ports_extension_->get(plugin_, 0u, false, &info) && info.channel_count > 0) {
		output_channels_ = info.channel_count;
	}
	return input_channels_ > 0 && output_channels_ > 0;
}

bool ClapExternalPluginHost::SyncParameterSnapshot() {
	parameter_bridge_.Clear();
	if (params_extension_ == nullptr || params_extension_->count == nullptr || params_extension_->get_info == nullptr) {
		return true;
	}
	const uint32_t parameter_count = params_extension_->count(plugin_);
	for (uint32_t index = 0; index < parameter_count; ++index) {
		ClapAbi::clap_param_info info{};
		if (!params_extension_->get_info(plugin_, index, &info)) {
			continue;
		}
		double current_value = info.default_value;
		if (params_extension_->get_value != nullptr) {
			params_extension_->get_value(plugin_, info.id, &current_value);
		}
		PluginParameterInfo parameter_info;
		parameter_info.id = info.id;
		parameter_info.name = info.name;
		parameter_info.module = info.module;
		parameter_info.flags = info.flags;
		parameter_info.is_stepped = (info.flags & ClapAbi::CLAP_PARAM_IS_STEPPED) != 0u;
		parameter_info.is_automatable = (info.flags & ClapAbi::CLAP_PARAM_IS_AUTOMATABLE) != 0u;
		parameter_info.min_value = static_cast<float>(info.min_value);
		parameter_info.max_value = static_cast<float>(info.max_value);
		parameter_info.default_value = static_cast<float>(info.default_value);
		parameter_info.current_value = static_cast<float>(current_value);
		if (param_metadata_extension_ != nullptr && param_metadata_extension_->get != nullptr) {
			ClapAbi::clap_param_metadata_info metadata_info{};
			if (param_metadata_extension_->get(plugin_, info.id, &metadata_info)) {
				parameter_info.unit = metadata_info.unit;
				parameter_info.ui_hint = metadata_info.ui_hint;
				parameter_info.description = metadata_info.description;
				if (parameter_info.module.empty()) {
					parameter_info.module = metadata_info.group;
				}
			}
		}
		parameter_bridge_.RegisterParameter(parameter_info);
		parameter_bridge_.SetValue(info.id, static_cast<float>(current_value));
	}
	return true;
}

bool ClapExternalPluginHost::Process(const float* input, float* output, uint32_t frames) {
	if (plugin_ == nullptr || input == nullptr || output == nullptr || frames == 0 || frames > max_block_size_) {
		return false;
	}
	std::vector<std::vector<float>> input_storage(input_channels_, std::vector<float>(frames, 0.0f));
	std::vector<std::vector<float>> output_storage(output_channels_, std::vector<float>(frames, 0.0f));
	std::vector<float*> input_ptrs(input_channels_, nullptr);
	std::vector<float*> output_ptrs(output_channels_, nullptr);
	for (uint32_t channel = 0; channel < input_channels_; ++channel) {
		input_storage[channel].assign(input, input + frames);
		input_ptrs[channel] = input_storage[channel].data();
	}
	for (uint32_t channel = 0; channel < output_channels_; ++channel) {
		output_ptrs[channel] = output_storage[channel].data();
	}

	ClapAbi::clap_audio_buffer input_buffer{input_ptrs.data(), nullptr, input_channels_, 0u, 0u};
	ClapAbi::clap_audio_buffer output_buffer{output_ptrs.data(), nullptr, output_channels_, 0u, 0u};
	const ClapAbi::clap_input_events empty_input_events{nullptr, &ClapExternalPluginHost::InputEventsSize, &ClapExternalPluginHost::InputEventsGet};
	const ClapAbi::clap_output_events empty_output_events{nullptr, &ClapExternalPluginHost::OutputEventsTryPush};
	const ClapAbi::clap_process process{
		-1,
		frames,
		nullptr,
		&input_buffer,
		&output_buffer,
		1u,
		1u,
		&empty_input_events,
		&empty_output_events,
	};
	const ClapAbi::clap_process_status status = plugin_->process(plugin_, &process);
	if (status == ClapAbi::CLAP_PROCESS_ERROR) {
		return false;
	}
	if (output_channels_ == 1) {
		std::copy(output_storage[0].begin(), output_storage[0].end(), output);
		return true;
	}
	for (uint32_t frame = 0; frame < frames; ++frame) {
		float mixed = 0.0f;
		for (uint32_t channel = 0; channel < output_channels_; ++channel) {
			mixed += output_storage[channel][frame];
		}
		output[frame] = mixed / static_cast<float>(output_channels_);
	}
	return true;
}

bool ClapExternalPluginHost::SetParameter(uint32_t id, float value) {
	if (plugin_ == nullptr) {
		return false;
	}
	PluginParameterInfo info;
	if (!parameter_bridge_.GetInfo(id, &info)) {
		return false;
	}
	const float clamped_value = std::clamp(value, info.min_value, info.max_value);
	if (!parameter_bridge_.SetValue(id, clamped_value)) {
		return false;
	}
	if (params_extension_ == nullptr || params_extension_->flush == nullptr) {
		return true;
	}
	const ClapAbi::clap_event_param_value event{
		{sizeof(ClapAbi::clap_event_param_value), 0u, ClapAbi::CLAP_CORE_EVENT_SPACE_ID, ClapAbi::CLAP_EVENT_PARAM_VALUE, 0u},
		id,
		nullptr,
		kWildcardNote,
		static_cast<int16_t>(kWildcardNote),
		static_cast<int16_t>(kWildcardNote),
		static_cast<int16_t>(kWildcardNote),
		clamped_value,
	};
	SingleEventList event_list{&event.header};
	const ClapAbi::clap_input_events input_events{&event_list, &ClapExternalPluginHost::InputEventsSize, &ClapExternalPluginHost::InputEventsGet};
	const ClapAbi::clap_output_events output_events{nullptr, &ClapExternalPluginHost::OutputEventsTryPush};
	params_extension_->flush(plugin_, &input_events, &output_events);
	return true;
}

bool ClapExternalPluginHost::GetParameter(uint32_t id, float* value) const {
	if (value == nullptr) {
		return false;
	}
	if (params_extension_ != nullptr && params_extension_->get_value != nullptr) {
		double plugin_value = 0.0;
		if (params_extension_->get_value(plugin_, id, &plugin_value)) {
			*value = static_cast<float>(plugin_value);
			return true;
		}
	}
	return parameter_bridge_.GetValue(id, *value);
}

std::vector<PluginParameterInfo> ClapExternalPluginHost::GetParameters() const {
	return parameter_bridge_.Snapshot();
}

ClapPluginMetadata ClapExternalPluginHost::GetMetadataPluginClap() const {
	ClapPluginMetadata metadata = loaded_metadata_;
	metadata.parameters = parameter_bridge_.Snapshot();
	return metadata;
}

std::string ClapExternalPluginHost::GetLoadedIdentifier() const {
	return loaded_identifier_;
}

const void* ClapExternalPluginHost::HostGetExtension(const ClapAbi::clap_host* host, const char* extension_id) {
	(void)host;
	(void)extension_id;
	return nullptr;
}

void ClapExternalPluginHost::HostRequestRestart(const ClapAbi::clap_host* host) {
	(void)host;
}

void ClapExternalPluginHost::HostRequestProcess(const ClapAbi::clap_host* host) {
	(void)host;
}

void ClapExternalPluginHost::HostRequestCallback(const ClapAbi::clap_host* host) {
	(void)host;
}

uint32_t ClapExternalPluginHost::InputEventsSize(const ClapAbi::clap_input_events* list) {
	if (list == nullptr || list->ctx == nullptr) {
		return 0u;
	}
	const SingleEventList* events = static_cast<const SingleEventList*>(list->ctx);
	return events->event != nullptr ? 1u : 0u;
}

const ClapAbi::clap_event_header* ClapExternalPluginHost::InputEventsGet(const ClapAbi::clap_input_events* list, uint32_t index) {
	if (index > 0 || list == nullptr || list->ctx == nullptr) {
		return nullptr;
	}
	const SingleEventList* events = static_cast<const SingleEventList*>(list->ctx);
	return events->event;
}

bool ClapExternalPluginHost::OutputEventsTryPush(const ClapAbi::clap_output_events* list, const ClapAbi::clap_event_header* event) {
	(void)list;
	(void)event;
	return true;
}

}  // namespace XER::Audio::Plugin