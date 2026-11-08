
#pragma once
#include "native/plugin/pxer/pxer_plugin_api.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace XER::Native::Plugin::Pxer {

struct PluginSummary {
	uint64_t instance_id = 0;
	std::string id;
	std::string name;
	std::string version;
	std::string description;
	std::string author;
	std::string path;
	bool loaded = false;
	std::size_t pending_async = 0;
	std::vector<std::string> registered_calls;
	std::vector<std::string> event_subscriptions;
};

struct EventRecord {
	uint64_t sequence = 0;
	std::string topic;
	std::string payload;
	std::string source_plugin;
};

struct AsyncTaskRecord {
	uint64_t task_id = 0;
	uint64_t plugin_instance_id = 0;
	std::string plugin_id;
	std::string task_name;
	std::string payload;
	std::string result;
	bool completed = false;
	bool success = false;
	std::string error;
};

class PluginHost {
public:
		// PXER: Диагностика и управление расширениями по instance_id
		std::vector<std::string> ListJsModulesByInstance(uint64_t instance_id);
		std::vector<std::string> ListFluxTypesByInstance(uint64_t instance_id);
		std::vector<std::string> ListFluxTriggersByInstance(uint64_t instance_id);
		std::vector<std::string> ListGlobalHooksByInstance(uint64_t instance_id);
		void RemoveJsModule(const std::string& name);
		void RemoveFluxType(const std::string& name);
		void RemoveFluxTrigger(const std::string& name);
		void RemoveGlobalHook(const std::string& name);
	static PluginHost& Shared();
	struct PluginRecord;

	bool Available() const;
	std::string AvailabilitySummary() const;

	bool Load(const std::filesystem::path& plugin_path,
		  PluginSummary* summary_out,
		  std::string* error_out);
	bool Unload(uint64_t instance_id, std::string* error_out);
	std::size_t UnloadAll(std::string* error_out);

	std::vector<PluginSummary> List() const;
	bool Describe(uint64_t instance_id, PluginSummary* summary_out, std::string* error_out) const;
	bool ResolveInstanceId(const std::string& plugin_key, uint64_t* instance_id_out, std::string* error_out) const;

	bool Invoke(uint64_t instance_id,
		    const std::string& call_name,
		    const std::string& payload,
		    std::string* result_out,
		    std::string* error_out);
	bool Call(const std::string& call_name,
		  const std::string& payload,
		  std::string* result_out,
		  std::string* error_out);

	bool EmitEvent(const std::string& topic,
		       const std::string& payload,
		       std::string* error_out);
	std::vector<EventRecord> History(const std::string& topic = std::string(),
					 std::size_t limit = 0) const;
	std::vector<AsyncTaskRecord> DrainCompletedAsync(std::size_t limit = 0);

	void SetSetting(const std::string& key, const std::string& value);
	std::string GetSetting(const std::string& key, const std::string& fallback = std::string()) const;
	bool RemoveSetting(const std::string& key);
	std::vector<std::string> SettingKeys() const;

	void SetConfig(const std::string& key, const std::string& value);
	std::string GetConfig(const std::string& key, const std::string& fallback = std::string()) const;
	bool RemoveConfig(const std::string& key);
	std::vector<std::string> ConfigKeys() const;
	std::vector<std::string> RegisteredCalls() const;
        
    // // --- PXER EXTENSIONS: PUBLIC ACCESSORS ---
    // std::unordered_map<std::string, void*> GetJsModuleBuilders() const;
    // std::unordered_map<std::string, void*> GetFluxTypes() const;
    // std::unordered_map<std::string, void*> GetFluxTriggers() const;
    // std::unordered_map<std::string, void*> GetGlobalHooks() const;
    // // PXER EXTENSIONS: JS/V8, Flux, Hooks

private:
	PluginHost();

	static void HostLog(PxerPluginHandle* handle, int level, const char* message);
	static int HostSetSetting(PxerPluginHandle* handle, const char* key, const char* value);
	static const char* HostGetSetting(PxerPluginHandle* handle, const char* key, const char* fallback);
	static int HostRemoveSetting(PxerPluginHandle* handle, const char* key);
	static int HostSetConfig(PxerPluginHandle* handle, const char* key, const char* value);
	static const char* HostGetConfig(PxerPluginHandle* handle, const char* key, const char* fallback);
	static int HostRemoveConfig(PxerPluginHandle* handle, const char* key);
	static int HostRegisterCall(PxerPluginHandle* handle, const char* call_name);
	static int HostUnregisterCall(PxerPluginHandle* handle, const char* call_name);
	static int HostSubscribeEvent(PxerPluginHandle* handle, const char* topic);
	static int HostUnsubscribeEvent(PxerPluginHandle* handle, const char* topic);
	static int HostEmitEvent(PxerPluginHandle* handle, const char* topic, const char* payload);
	static uint64_t HostScheduleAsync(PxerPluginHandle* handle, const char* task_name, const char* payload);
    static int HostRegisterJsModule(PxerPluginHandle* handle, const char* module_name, void* builder_fn_ptr);
    static int HostRegisterFluxType(PxerPluginHandle* handle, const char* type_name, void* type_info_ptr);
    static int HostRegisterFluxTrigger(PxerPluginHandle* handle, const char* trigger_name, void* trigger_info_ptr);
    static int HostRegisterGlobalHook(PxerPluginHandle* handle, const char* hook_name, void* hook_fn_ptr);

	PxerHostApi host_api_{};
};

}  // namespace XER::Native::Plugin::Pxer