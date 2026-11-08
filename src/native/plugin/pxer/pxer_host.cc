

#include "native/plugin/pxer/pxer_host.h"
#include "native/plugin/pxer/pxer_plugin_api.h"

#include "async_io/io_thread_pool.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
 
#include <dlfcn.h>
#include <ffi.h> 

namespace XER::Native::Plugin::Pxer {
namespace {

std::string SafeString(const char* text) {
	return text != nullptr ? std::string(text) : std::string();
}

template <typename T>
std::vector<std::string> SortedStrings(const T& values) {
	std::vector<std::string> out(values.begin(), values.end());
	std::sort(out.begin(), out.end());
	return out;
}

template <typename MapType>
std::vector<std::string> SortedMapKeys(const MapType& values) {
	std::vector<std::string> out;
	out.reserve(values.size());
	for (const auto& entry : values) {
		out.push_back(entry.first);
	}
	std::sort(out.begin(), out.end());
	return out;
}

bool TopicMatches(const std::string& subscription, const std::string& topic) {
	return subscription.empty() || subscription == "*" || subscription == topic;
}
 
std::string DlErrorOr(const char* fallback) {
	const char* error = ::dlerror();
	return error != nullptr ? std::string(error) : std::string(fallback);
}

bool PrepareCif(ffi_cif* cif,
		ffi_type* return_type,
		unsigned int argument_count,
		ffi_type** argument_types,
		std::string* error_out) {
	if (cif == nullptr) {
		if (error_out != nullptr) {
			*error_out = "ffi cif target is null";
		}
		return false;
	}
	if (ffi_prep_cif(cif, FFI_DEFAULT_ABI, argument_count, return_type, argument_types) != FFI_OK) {
		if (error_out != nullptr) {
			*error_out = "ffi_prep_cif failed";
		}
		return false;
	}
	return true;
}

bool CallGetPluginApi(void* symbol,
		      const PxerPluginApi** api_out,
		      std::string* error_out) {
	if (api_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "plugin api output target is null";
		}
		return false;
	}
	ffi_cif cif;
	if (!PrepareCif(&cif, &ffi_type_pointer, 0u, nullptr, error_out)) {
		return false;
	}
	void* result = nullptr;
	ffi_call(&cif, FFI_FN(symbol), &result, nullptr);
	*api_out = static_cast<const PxerPluginApi*>(result);
	return *api_out != nullptr;
}

bool CallLifecycle(PxerPluginLifecycleFn function,
		   const PxerHostApi* host_api,
		   PxerPluginHandle* handle,
		   std::string* error_out) {
	ffi_cif cif;
	ffi_type* argument_types[2] = {&ffi_type_pointer, &ffi_type_pointer};
	if (!PrepareCif(&cif, &ffi_type_sint, 2u, argument_types, error_out)) {
		return false;
	}
	const PxerHostApi* host_arg = host_api;
	PxerPluginHandle* handle_arg = handle;
	void* arguments[2] = {&host_arg, &handle_arg};
	int result = 0;
	ffi_call(&cif, FFI_FN(function), &result, arguments);
	if (result != 0) {
		if (error_out != nullptr && error_out->empty()) {
			*error_out = "plugin lifecycle hook failed";
		}
		return false;
	}
	return true;
}

bool CallInvoke(PxerPluginInvokeFn function,
		const PxerHostApi* host_api,
		PxerPluginHandle* handle,
		const char* call_name,
		const char* payload,
		PxerOwnedString* out_result,
		std::string* error_out) {
	ffi_cif cif;
	ffi_type* argument_types[5] = {
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer,
	};
	if (!PrepareCif(&cif, &ffi_type_sint, 5u, argument_types, error_out)) {
		return false;
	}
	const PxerHostApi* host_arg = host_api;
	PxerPluginHandle* handle_arg = handle;
	const char* call_arg = call_name;
	const char* payload_arg = payload;
	PxerOwnedString* result_arg = out_result;
	void* arguments[5] = {&host_arg, &handle_arg, &call_arg, &payload_arg, &result_arg};
	int result = 0;
	ffi_call(&cif, FFI_FN(function), &result, arguments);
	if (result != 0) {
		if (error_out != nullptr && error_out->empty()) {
			*error_out = "plugin invoke hook failed";
		}
		return false;
	}
	return true;
}

bool CallEvent(PxerPluginEventFn function,
	       const PxerHostApi* host_api,
	       PxerPluginHandle* handle,
	       const char* topic,
	       const char* payload,
	       std::string* error_out) {
	ffi_cif cif;
	ffi_type* argument_types[4] = {
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer,
	};
	if (!PrepareCif(&cif, &ffi_type_sint, 4u, argument_types, error_out)) {
		return false;
	}
	const PxerHostApi* host_arg = host_api;
	PxerPluginHandle* handle_arg = handle;
	const char* topic_arg = topic;
	const char* payload_arg = payload;
	void* arguments[4] = {&host_arg, &handle_arg, &topic_arg, &payload_arg};
	int result = 0;
	ffi_call(&cif, FFI_FN(function), &result, arguments);
	if (result != 0) {
		if (error_out != nullptr && error_out->empty()) {
			*error_out = "plugin event hook failed";
		}
		return false;
	}
	return true;
}

bool CallAsync(PxerPluginAsyncFn function,
	       const PxerHostApi* host_api,
	       PxerPluginHandle* handle,
	       uint64_t task_id,
	       const char* task_name,
	       const char* payload,
	       PxerOwnedString* out_result,
	       std::string* error_out) {
	ffi_cif cif;
	ffi_type* argument_types[6] = {
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_uint64,
		&ffi_type_pointer,
		&ffi_type_pointer,
		&ffi_type_pointer,
	};
	if (!PrepareCif(&cif, &ffi_type_sint, 6u, argument_types, error_out)) {
		return false;
	}
	const PxerHostApi* host_arg = host_api;
	PxerPluginHandle* handle_arg = handle;
	uint64_t task_id_arg = task_id;
	const char* task_name_arg = task_name;
	const char* payload_arg = payload;
	PxerOwnedString* result_arg = out_result;
	void* arguments[6] = {
		&host_arg,
		&handle_arg,
		&task_id_arg,
		&task_name_arg,
		&payload_arg,
		&result_arg,
	};
	int result = 0;
	ffi_call(&cif, FFI_FN(function), &result, arguments);
	if (result != 0) {
		if (error_out != nullptr && error_out->empty()) {
			*error_out = "plugin async hook failed";
		}
		return false;
	}
	return true;
} 

}  // namespace
}  // namespace XER::Native::Plugin::Pxer

struct PxerPluginHandle {
	XER::Native::Plugin::Pxer::PluginHost* host = nullptr;
	uint64_t instance_id = 0;
};

namespace XER::Native::Plugin::Pxer {

struct PluginHost::PluginRecord {
	uint64_t instance_id = 0;
	void* library_handle = nullptr;
	const PxerPluginApi* api = nullptr;
	PxerPluginHandle handle;
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::string author;
	std::string path;
	bool loaded = false;
	std::size_t pending_async = 0;
	std::unordered_set<std::string> registered_calls;
	std::unordered_set<std::string> subscriptions;
};

class PluginHostState {
public:
	std::unordered_map<uint64_t, std::unique_ptr<PluginHost::PluginRecord>> plugins;
	std::unordered_map<std::string, uint64_t> call_overrides;
	std::unordered_map<std::string, std::string> settings;
	std::unordered_map<std::string, std::string> config;
	std::vector<EventRecord> events;
	std::deque<AsyncTaskRecord> completed_async;
	// PXER EXTENSIONS:
	using ExtValue = std::pair<uint64_t, void*>; // (instance_id, ptr)
	std::unordered_map<std::string, ExtValue> js_module_builders; // name -> (instance_id, builder_fn_ptr)
	std::unordered_map<std::string, ExtValue> flux_types;         // name -> (instance_id, type_info_ptr)
	std::unordered_map<std::string, ExtValue> flux_triggers;      // name -> (instance_id, trigger_info_ptr)
	std::unordered_map<std::string, ExtValue> global_hooks;       // name -> (instance_id, hook_fn_ptr)

	uint64_t next_plugin_instance_id = 1;
	uint64_t next_event_sequence = 1;
	uint64_t next_async_task_id = 1;
	mutable std::mutex mutex;
};

PluginHostState& MutableState() {
	static PluginHostState state;
	return state;
}

PluginSummary MakeSummary(const PluginHost::PluginRecord& record) {
	PluginSummary summary;
	summary.instance_id = record.instance_id;
	summary.id = record.id;
	summary.name = record.name;
	summary.version = record.version;
	summary.description = record.description;
	summary.author = record.author;
	summary.path = record.path;
	summary.loaded = record.loaded;
	summary.pending_async = record.pending_async;
	summary.registered_calls = SortedStrings(record.registered_calls);
	summary.event_subscriptions = SortedStrings(record.subscriptions);
	return summary;
}

void AppendEventLocked(PluginHostState& state,
		       const std::string& topic,
		       const std::string& payload,
		       const std::string& source_plugin) {
	EventRecord record;
	record.sequence = state.next_event_sequence++;
	record.topic = topic;
	record.payload = payload;
	record.source_plugin = source_plugin;
	state.events.push_back(std::move(record));
}

PluginHost& PluginHost::Shared() {
	static PluginHost host;
	return host;
}

PluginHost::PluginHost() {
	host_api_.api_version = PXER_PLUGIN_API_VERSION;
	host_api_.log = &PluginHost::HostLog;
	host_api_.set_setting = &PluginHost::HostSetSetting;
	host_api_.get_setting = &PluginHost::HostGetSetting;
	host_api_.remove_setting = &PluginHost::HostRemoveSetting;
	host_api_.set_config = &PluginHost::HostSetConfig;
	host_api_.get_config = &PluginHost::HostGetConfig;
	host_api_.remove_config = &PluginHost::HostRemoveConfig;
	host_api_.register_call = &PluginHost::HostRegisterCall;
	host_api_.unregister_call = &PluginHost::HostUnregisterCall;
	host_api_.subscribe_event = &PluginHost::HostSubscribeEvent;
	host_api_.unsubscribe_event = &PluginHost::HostUnsubscribeEvent;
	host_api_.emit_event = &PluginHost::HostEmitEvent;
	host_api_.schedule_async = &PluginHost::HostScheduleAsync;

	// PXER EXTENSIONS: JS/V8, Flux, Hooks
	host_api_.register_js_module = &PluginHost::HostRegisterJsModule;
	host_api_.register_flux_type = &PluginHost::HostRegisterFluxType;
	host_api_.register_flux_trigger = &PluginHost::HostRegisterFluxTrigger;
	host_api_.register_global_hook = &PluginHost::HostRegisterGlobalHook;
}

bool PluginHost::Available() const { 
	return true; 
}

std::string PluginHost::AvailabilitySummary() const { 
	return "PXER native plugins available via libffi"; 
}

bool PluginHost::Load(const std::filesystem::path& plugin_path,
		      PluginSummary* summary_out,
		      std::string* error_out) { 
	const std::filesystem::path resolved_path = std::filesystem::absolute(plugin_path).lexically_normal();
	::dlerror();
	void* library_handle = ::dlopen(resolved_path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (library_handle == nullptr) {
		if (error_out != nullptr) {
			*error_out = DlErrorOr("dlopen failed");
		}
		return false;
	}
	::dlerror();
	void* symbol = ::dlsym(library_handle, "pxer_get_plugin_api");
	if (symbol == nullptr) {
		if (error_out != nullptr) {
			*error_out = DlErrorOr("plugin does not export pxer_get_plugin_api");
		}
		::dlclose(library_handle);
		return false;
	}
	const PxerPluginApi* api = nullptr;
	std::string local_error;
	if (!CallGetPluginApi(symbol, &api, &local_error) || api == nullptr || api->descriptor == nullptr) {
		if (error_out != nullptr) {
			*error_out = local_error.empty() ? "pxer_get_plugin_api returned an invalid plugin api" : local_error;
		}
		::dlclose(library_handle);
		return false;
	}
	if (api->descriptor->api_version != PXER_PLUGIN_API_VERSION) {
		if (error_out != nullptr) {
			*error_out = "plugin api version mismatch";
		}
		::dlclose(library_handle);
		return false;
	}
	auto record = std::make_unique<PluginRecord>();
	record->instance_id = 0;
	record->library_handle = library_handle;
	record->api = api;
	record->handle.host = this;
	record->id = SafeString(api->descriptor->id);
	record->name = SafeString(api->descriptor->name);
	record->version = SafeString(api->descriptor->version);
	record->description = SafeString(api->descriptor->description);
	record->author = SafeString(api->descriptor->author);
	record->path = resolved_path.string();
	if (record->id.empty()) {
		if (error_out != nullptr) {
			*error_out = "plugin descriptor id is required";
		}
		::dlclose(library_handle);
		return false;
	}
	PluginHostState& state = MutableState();
	uint64_t instance_id = 0;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		record->instance_id = state.next_plugin_instance_id++;
		instance_id = record->instance_id;
		record->handle.instance_id = record->instance_id;
		state.plugins.emplace(record->instance_id, std::move(record));
	}
	PluginRecord* stored_record = nullptr;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(instance_id);
		if (iterator != state.plugins.end()) {
			stored_record = iterator->second.get();
		}
	}
	if (stored_record == nullptr) {
		if (error_out != nullptr) {
			*error_out = "failed to store plugin state";
		}
		::dlclose(library_handle);
		return false;
	}
	if (stored_record->api->on_load != nullptr) {
		local_error.clear();
		if (!CallLifecycle(stored_record->api->on_load, &host_api_, &stored_record->handle, &local_error)) {
			Unload(stored_record->instance_id, nullptr);
			if (error_out != nullptr) {
				*error_out = local_error.empty() ? "plugin on_load failed" : local_error;
			}
			return false;
		}
	}
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(stored_record->instance_id);
		if (iterator != state.plugins.end()) {
			iterator->second->loaded = true;
			if (summary_out != nullptr) {
				*summary_out = MakeSummary(*iterator->second);
			}
		}
	}
	return true; 
}

bool PluginHost::Unload(uint64_t instance_id, std::string* error_out) {
	PluginHostState& state = MutableState();
	PluginRecord* record = nullptr;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(instance_id);
		if (iterator == state.plugins.end()) {
			if (error_out != nullptr) {
				*error_out = "plugin instance not found";
			}
			return false;
		}
		if (iterator->second->pending_async != 0u) {
			if (error_out != nullptr) {
				*error_out = "plugin has pending async work";
			}
			return false;
		}
		record = iterator->second.get();

		// PXER: удалить все расширения, связанные с этим плагином
		auto erase_by_instance = [instance_id](auto& map, const char* what) {
			std::vector<std::string> to_erase;
			for (const auto& entry : map) {
				if (entry.second.first == instance_id) {
					to_erase.push_back(entry.first);
				}
			}
			for (const auto& name : to_erase) {
				map.erase(name);
				// Логгирование
				fprintf(stderr, "PXER: erased %s '%s' for instance %llu\n", what, name.c_str(), (unsigned long long)instance_id);
			}
		};
		erase_by_instance(state.js_module_builders, "js_module");
		erase_by_instance(state.flux_types, "flux_type");
		erase_by_instance(state.flux_triggers, "flux_trigger");
		erase_by_instance(state.global_hooks, "global_hook");
	} 
	if (record != nullptr && record->api != nullptr && record->api->on_unload != nullptr) {
		std::string local_error;
		if (!CallLifecycle(record->api->on_unload, &host_api_, &record->handle, &local_error)) {
			if (error_out != nullptr) {
				*error_out = local_error.empty() ? "plugin on_unload failed" : local_error;
			}
			return false;
		}
	} 
	void* library_handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(instance_id);
		if (iterator == state.plugins.end()) {
			return true;
		}
		for (const std::string& call_name : iterator->second->registered_calls) {
			auto call_iterator = state.call_overrides.find(call_name);
			if (call_iterator != state.call_overrides.end() && call_iterator->second == instance_id) {
				state.call_overrides.erase(call_iterator);
			}
		}
		library_handle = iterator->second->library_handle;
		state.plugins.erase(iterator);
	} 
	if (library_handle != nullptr) {
		::dlclose(library_handle);
	} 
	return true;
}

std::size_t PluginHost::UnloadAll(std::string* error_out) {
	std::vector<uint64_t> instance_ids;
	{
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		instance_ids.reserve(state.plugins.size());
		for (const auto& entry : state.plugins) {
			instance_ids.push_back(entry.first);
		}
	}
	std::size_t unloaded_count = 0;
	std::string errors;
	for (uint64_t instance_id : instance_ids) {
		std::string local_error;
		if (Unload(instance_id, &local_error)) {
			++unloaded_count;
		} else if (!local_error.empty()) {
			if (!errors.empty()) {
				errors += "; ";
			}
			errors += local_error;
		}
	}
	if (error_out != nullptr) {
		*error_out = errors;
	}
	return unloaded_count;
}

std::vector<PluginSummary> PluginHost::List() const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<PluginSummary> summaries;
	summaries.reserve(state.plugins.size());
	for (const auto& entry : state.plugins) {
		summaries.push_back(MakeSummary(*entry.second));
	}
	std::sort(summaries.begin(), summaries.end(), [](const PluginSummary& left, const PluginSummary& right) {
		return left.instance_id < right.instance_id;
	});
	return summaries;
}

bool PluginHost::Describe(uint64_t instance_id, PluginSummary* summary_out, std::string* error_out) const {
	if (summary_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "plugin summary output target is null";
		}
		return false;
	}
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.plugins.find(instance_id);
	if (iterator == state.plugins.end()) {
		if (error_out != nullptr) {
			*error_out = "plugin instance not found";
		}
		return false;
	}
	*summary_out = MakeSummary(*iterator->second);
	return true;
}

bool PluginHost::ResolveInstanceId(const std::string& plugin_key,
			   uint64_t* instance_id_out,
			   std::string* error_out) const {
	if (instance_id_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "plugin instance output target is null";
		}
		return false;
	}
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	for (const auto& entry : state.plugins) {
		if (entry.second->id == plugin_key || entry.second->name == plugin_key || entry.second->path == plugin_key) {
			*instance_id_out = entry.first;
			return true;
		}
	}
	if (error_out != nullptr) {
		*error_out = "plugin key not found";
	}
	return false;
}

bool PluginHost::Invoke(uint64_t instance_id,
			const std::string& call_name,
			const std::string& payload,
			std::string* result_out,
			std::string* error_out) {
	if (result_out == nullptr) {
		if (error_out != nullptr) {
			*error_out = "plugin result output target is null";
		}
		return false;
	}
	PluginRecord* record = nullptr;
	{
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(instance_id);
		if (iterator == state.plugins.end()) {
			if (error_out != nullptr) {
				*error_out = "plugin instance not found";
			}
			return false;
		}
		record = iterator->second.get();
	}
	if (record == nullptr || record->api == nullptr || record->api->invoke == nullptr) {
		if (error_out != nullptr) {
			*error_out = "plugin does not provide invoke hook";
		}
		return false;
	}
	PxerOwnedString plugin_result{};
	std::string local_error; 
	if (!CallInvoke(record->api->invoke,
			&host_api_,
			&record->handle,
			call_name.c_str(),
			payload.c_str(),
			&plugin_result,
			&local_error)) {
		if (record->api->release_string != nullptr && plugin_result.data != nullptr) {
			record->api->release_string(&plugin_result);
		}
		if (error_out != nullptr) {
			*error_out = local_error.empty() ? "plugin invocation failed" : local_error;
		}
		return false;
	} 
	result_out->assign(plugin_result.data != nullptr ? plugin_result.data : "", plugin_result.size);
	if (record->api->release_string != nullptr && plugin_result.data != nullptr) {
		record->api->release_string(&plugin_result);
	}
	return true;
}

bool PluginHost::Call(const std::string& call_name,
		  const std::string& payload,
		  std::string* result_out,
		  std::string* error_out) {
	uint64_t instance_id = 0;
	{
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.call_overrides.find(call_name);
		if (iterator == state.call_overrides.end()) {
			if (error_out != nullptr) {
				*error_out = "call is not registered by any PXER plugin";
			}
			return false;
		}
		instance_id = iterator->second;
	}
	return Invoke(instance_id, call_name, payload, result_out, error_out);
}

bool PluginHost::EmitEvent(const std::string& topic,
			   const std::string& payload,
			   std::string* error_out) {
	std::vector<uint64_t> recipients;
	{
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		AppendEventLocked(state, topic, payload, "host");
		for (const auto& entry : state.plugins) {
			for (const std::string& subscription : entry.second->subscriptions) {
				if (TopicMatches(subscription, topic)) {
					recipients.push_back(entry.first);
					break;
				}
			}
		}
	}
	std::string first_error; 
	for (uint64_t recipient_id : recipients) {
		PluginRecord* record = nullptr;
		{
			PluginHostState& state = MutableState();
			std::lock_guard<std::mutex> lock(state.mutex);
			auto iterator = state.plugins.find(recipient_id);
			if (iterator != state.plugins.end()) {
				record = iterator->second.get();
			}
		}
		if (record == nullptr || record->api == nullptr || record->api->on_event == nullptr) {
			continue;
		}
		std::string local_error;
		if (!CallEvent(record->api->on_event,
				&host_api_,
				&record->handle,
				topic.c_str(),
				payload.c_str(),
				&local_error)) {
			if (first_error.empty()) {
				first_error = local_error;
			}
		}
	} 
	if (!first_error.empty()) {
		if (error_out != nullptr) {
			*error_out = first_error;
		}
		return false;
	}
	return true;
}

std::vector<EventRecord> PluginHost::History(const std::string& topic, std::size_t limit) const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<EventRecord> out;
	for (const EventRecord& record : state.events) {
		if (topic.empty() || record.topic == topic) {
			out.push_back(record);
		}
	}
	if (limit > 0 && out.size() > limit) {
		out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
	}
	return out;
}

std::vector<AsyncTaskRecord> PluginHost::DrainCompletedAsync(std::size_t limit) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<AsyncTaskRecord> out;
	const std::size_t count = limit == 0 ? state.completed_async.size() : std::min(limit, state.completed_async.size());
	out.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		out.push_back(std::move(state.completed_async.front()));
		state.completed_async.pop_front();
	}
	return out;
}

void PluginHost::SetSetting(const std::string& key, const std::string& value) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.settings[key] = value;
}

std::string PluginHost::GetSetting(const std::string& key, const std::string& fallback) const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.settings.find(key);
	return iterator != state.settings.end() ? iterator->second : fallback;
}

bool PluginHost::RemoveSetting(const std::string& key) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	return state.settings.erase(key) != 0u;
}

std::vector<std::string> PluginHost::SettingKeys() const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	return SortedMapKeys(state.settings);
}

void PluginHost::SetConfig(const std::string& key, const std::string& value) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.config[key] = value;
}

std::string PluginHost::GetConfig(const std::string& key, const std::string& fallback) const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.config.find(key);
	return iterator != state.config.end() ? iterator->second : fallback;
}

bool PluginHost::RemoveConfig(const std::string& key) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	return state.config.erase(key) != 0u;
}

std::vector<std::string> PluginHost::ConfigKeys() const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	return SortedMapKeys(state.config);
}

std::vector<std::string> PluginHost::RegisteredCalls() const {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	return SortedMapKeys(state.call_overrides);
}

void PluginHost::HostLog(PxerPluginHandle* handle, int level, const char* message) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::string source = "plugin";
	if (handle != nullptr) {
		auto iterator = state.plugins.find(handle->instance_id);
		if (iterator != state.plugins.end()) {
			source = iterator->second->id;
		}
	}
	AppendEventLocked(state, "pxer.log." + std::to_string(level), SafeString(message), source);
}

int PluginHost::HostSetSetting(PxerPluginHandle* handle, const char* key, const char* value) {
	(void)handle;
	if (key == nullptr) {
		return 0;
	}
	Shared().SetSetting(key, SafeString(value));
	return 1;
}

const char* PluginHost::HostGetSetting(PxerPluginHandle* handle, const char* key, const char* fallback) {
	(void)handle;
	thread_local std::string storage;
	storage = Shared().GetSetting(SafeString(key), SafeString(fallback));
	return storage.c_str();
}

int PluginHost::HostRemoveSetting(PxerPluginHandle* handle, const char* key) {
	(void)handle;
	return key != nullptr && Shared().RemoveSetting(key) ? 1 : 0;
}

int PluginHost::HostSetConfig(PxerPluginHandle* handle, const char* key, const char* value) {
	(void)handle;
	if (key == nullptr) {
		return 0;
	}
	Shared().SetConfig(key, SafeString(value));
	return 1;
}

const char* PluginHost::HostGetConfig(PxerPluginHandle* handle, const char* key, const char* fallback) {
	(void)handle;
	thread_local std::string storage;
	storage = Shared().GetConfig(SafeString(key), SafeString(fallback));
	return storage.c_str();
}

int PluginHost::HostRemoveConfig(PxerPluginHandle* handle, const char* key) {
	(void)handle;
	return key != nullptr && Shared().RemoveConfig(key) ? 1 : 0;
}

int PluginHost::HostRegisterCall(PxerPluginHandle* handle, const char* call_name) {
	if (handle == nullptr || call_name == nullptr || call_name[0] == '\0') {
		return 0;
	}
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.plugins.find(handle->instance_id);
	if (iterator == state.plugins.end()) {
		return 0;
	}
	const std::string name(call_name);
	auto existing = state.call_overrides.find(name);
	if (existing != state.call_overrides.end()) {
		auto old_plugin = state.plugins.find(existing->second);
		if (old_plugin != state.plugins.end()) {
			old_plugin->second->registered_calls.erase(name);
		}
	}
	state.call_overrides[name] = handle->instance_id;
	iterator->second->registered_calls.insert(name);
	return 1;
}

int PluginHost::HostUnregisterCall(PxerPluginHandle* handle, const char* call_name) {
	if (handle == nullptr || call_name == nullptr) {
		return 0;
	}
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.plugins.find(handle->instance_id);
	if (iterator == state.plugins.end()) {
		return 0;
	}
	const std::string name(call_name);
	iterator->second->registered_calls.erase(name);
	auto call_iterator = state.call_overrides.find(name);
	if (call_iterator != state.call_overrides.end() && call_iterator->second == handle->instance_id) {
		state.call_overrides.erase(call_iterator);
	}
	return 1;
}

int PluginHost::HostSubscribeEvent(PxerPluginHandle* handle, const char* topic) {
	if (handle == nullptr || topic == nullptr) {
		return 0;
	}
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.plugins.find(handle->instance_id);
	if (iterator == state.plugins.end()) {
		return 0;
	}
	iterator->second->subscriptions.insert(topic);
	return 1;
}

int PluginHost::HostUnsubscribeEvent(PxerPluginHandle* handle, const char* topic) {
	if (handle == nullptr || topic == nullptr) {
		return 0;
	}
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	auto iterator = state.plugins.find(handle->instance_id);
	if (iterator == state.plugins.end()) {
		return 0;
	}
	iterator->second->subscriptions.erase(topic);
	return 1;
}

int PluginHost::HostEmitEvent(PxerPluginHandle* handle, const char* topic, const char* payload) {
	std::string error;
	if (!Shared().EmitEvent(SafeString(topic), SafeString(payload), &error)) {
		return 0;
	}
	if (handle != nullptr) {
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		if (!state.events.empty()) {
			state.events.back().source_plugin = std::to_string(handle->instance_id);
			auto iterator = state.plugins.find(handle->instance_id);
			if (iterator != state.plugins.end()) {
				state.events.back().source_plugin = iterator->second->id;
			}
		}
	}
	return 1;
}

uint64_t PluginHost::HostScheduleAsync(PxerPluginHandle* handle, const char* task_name, const char* payload) {
	if (handle == nullptr || task_name == nullptr) {
		return 0;
	}
	uint64_t task_id = 0;
	{
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(handle->instance_id);
		if (iterator == state.plugins.end() || iterator->second->api == nullptr || iterator->second->api->on_async == nullptr) {
			return 0;
		}
		task_id = state.next_async_task_id++;
		++iterator->second->pending_async;
	}
	const uint64_t instance_id = handle->instance_id;
	const std::string task_name_copy(task_name);
	const std::string payload_copy = SafeString(payload);
	::IO::AsyncIO::IOThreadPool::GetSharedInstance().Enqueue([instance_id, task_id, task_name_copy, payload_copy]() {
		AsyncTaskRecord completed;
		completed.task_id = task_id;
		completed.plugin_instance_id = instance_id;
		completed.task_name = task_name_copy;
		completed.payload = payload_copy;
		PluginHost& host = PluginHost::Shared();
		PluginRecord* plugin = nullptr;
		{
			PluginHostState& state = MutableState();
			std::lock_guard<std::mutex> lock(state.mutex);
			auto iterator = state.plugins.find(instance_id);
			if (iterator != state.plugins.end()) {
				plugin = iterator->second.get();
				completed.plugin_id = iterator->second->id;
			}
		}
		if (plugin == nullptr || plugin->api == nullptr || plugin->api->on_async == nullptr) {
			completed.completed = true;
			completed.success = false;
			completed.error = "plugin instance unavailable for async task";
		} else {
			PxerOwnedString plugin_result{};
			std::string local_error;
			if (CallAsync(plugin->api->on_async,
					&host.host_api_,
					&plugin->handle,
					task_id,
					task_name_copy.c_str(),
					payload_copy.c_str(),
					&plugin_result,
					&local_error)) {
				completed.completed = true;
				completed.success = true;
				completed.result.assign(plugin_result.data != nullptr ? plugin_result.data : "", plugin_result.size);
				if (plugin->api->release_string != nullptr && plugin_result.data != nullptr) {
					plugin->api->release_string(&plugin_result);
				}
			} else {
				if (plugin->api->release_string != nullptr && plugin_result.data != nullptr) {
					plugin->api->release_string(&plugin_result);
				}
				completed.completed = true;
				completed.success = false;
				completed.error = local_error.empty() ? "plugin async task failed" : local_error;
			}
		}
		PluginHostState& state = MutableState();
		std::lock_guard<std::mutex> lock(state.mutex);
		auto iterator = state.plugins.find(instance_id);
		if (iterator != state.plugins.end() && iterator->second->pending_async > 0u) {
			--iterator->second->pending_async;
		}
		state.completed_async.push_back(completed);
		AppendEventLocked(
			state,
			completed.success ? "pxer.async.completed" : "pxer.async.failed",
			completed.success ? completed.result : completed.error,
			completed.plugin_id.empty() ? std::to_string(instance_id) : completed.plugin_id);
	});
	return task_id;
}
// PXER EXTENSIONS: JS/V8, Flux, Hooks
int PluginHost::HostRegisterJsModule(PxerPluginHandle* handle, const char* module_name, void* builder_fn_ptr) {
	if (!handle || !module_name || !builder_fn_ptr) return 0;
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::string name(module_name);
	uint64_t iid = handle->instance_id;
	if (state.js_module_builders.count(name) && state.js_module_builders[name].first != iid) {
		HostLog(handle, 2, ("PXER: JS module name conflict: " + name).c_str());
		return 0;
	}
	state.js_module_builders[name] = {iid, builder_fn_ptr};
	HostLog(handle, 1, ("PXER: register_js_module saved: " + name).c_str());
	return 1;
}

int PluginHost::HostRegisterFluxType(PxerPluginHandle* handle, const char* type_name, void* type_info_ptr) {
	if (!handle || !type_name || !type_info_ptr) return 0;
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::string name(type_name);
	uint64_t iid = handle->instance_id;
	if (state.flux_types.count(name) && state.flux_types[name].first != iid) {
		HostLog(handle, 2, ("PXER: Flux type name conflict: " + name).c_str());
		return 0;
	}
	state.flux_types[name] = {iid, type_info_ptr};
	HostLog(handle, 1, ("PXER: register_flux_type saved: " + name).c_str());
	return 1;
}

int PluginHost::HostRegisterFluxTrigger(PxerPluginHandle* handle, const char* trigger_name, void* trigger_info_ptr) {
	if (!handle || !trigger_name || !trigger_info_ptr) return 0;
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::string name(trigger_name);
	uint64_t iid = handle->instance_id;
	if (state.flux_triggers.count(name) && state.flux_triggers[name].first != iid) {
		HostLog(handle, 2, ("PXER: Flux trigger name conflict: " + name).c_str());
		return 0;
	}
	state.flux_triggers[name] = {iid, trigger_info_ptr};
	HostLog(handle, 1, ("PXER: register_flux_trigger saved: " + name).c_str());
	return 1;
}

int PluginHost::HostRegisterGlobalHook(PxerPluginHandle* handle, const char* hook_name, void* hook_fn_ptr) {
	if (!handle || !hook_name || !hook_fn_ptr) return 0;
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::string name(hook_name);
	uint64_t iid = handle->instance_id;
	if (state.global_hooks.count(name) && state.global_hooks[name].first != iid) {
		HostLog(handle, 2, ("PXER: Global hook name conflict: " + name).c_str());
		return 0;
	}
	state.global_hooks[name] = {iid, hook_fn_ptr};
	HostLog(handle, 1, ("PXER: register_global_hook saved: " + name).c_str());
	return 1;
}

// --- PXER EXTENSIONS: PUBLIC ACCESSORS ---
// std::unordered_map<std::string, void*> PluginHost::GetJsModuleBuilders() const {
// 	PluginHostState& state = MutableState();
// 	std::lock_guard<std::mutex> lock(state.mutex);
// 	return state.js_module_builders;
// }

// std::unordered_map<std::string, void*> PluginHost::GetFluxTypes() const {
// 	PluginHostState& state = MutableState();
// 	std::lock_guard<std::mutex> lock(state.mutex);
// 	return state.flux_types;
// }

// std::unordered_map<std::string, void*> PluginHost::GetFluxTriggers() const {
// 	PluginHostState& state = MutableState();
// 	std::lock_guard<std::mutex> lock(state.mutex);
// 	return state.flux_triggers;
// }

// std::unordered_map<std::string, void*> PluginHost::GetGlobalHooks() const {
// 	PluginHostState& state = MutableState();
// 	std::lock_guard<std::mutex> lock(state.mutex);
// 	return state.global_hooks;
// }
// PXER: Диагностика и управление расширениями по instance_id
std::vector<std::string> PluginHost::ListJsModulesByInstance(uint64_t instance_id) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<std::string> result;
	for (const auto& entry : state.js_module_builders) {
		if (entry.second.first == instance_id) result.push_back(entry.first);
	}
	return result;
}

std::vector<std::string> PluginHost::ListFluxTypesByInstance(uint64_t instance_id) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<std::string> result;
	for (const auto& entry : state.flux_types) {
		if (entry.second.first == instance_id) result.push_back(entry.first);
	}
	return result;
}

std::vector<std::string> PluginHost::ListFluxTriggersByInstance(uint64_t instance_id) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<std::string> result;
	for (const auto& entry : state.flux_triggers) {
		if (entry.second.first == instance_id) result.push_back(entry.first);
	}
	return result;
}

std::vector<std::string> PluginHost::ListGlobalHooksByInstance(uint64_t instance_id) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	std::vector<std::string> result;
	for (const auto& entry : state.global_hooks) {
		if (entry.second.first == instance_id) result.push_back(entry.first);
	}
	return result;
}

void PluginHost::RemoveJsModule(const std::string& name) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.js_module_builders.erase(name);
}

void PluginHost::RemoveFluxType(const std::string& name) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.flux_types.erase(name);
}

void PluginHost::RemoveFluxTrigger(const std::string& name) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.flux_triggers.erase(name);
}

void PluginHost::RemoveGlobalHook(const std::string& name) {
	PluginHostState& state = MutableState();
	std::lock_guard<std::mutex> lock(state.mutex);
	state.global_hooks.erase(name);
}


}  // namespace XER::Native::Plugin::Pxer