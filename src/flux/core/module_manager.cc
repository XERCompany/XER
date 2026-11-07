#include "flux/core/module_manager.h"

#include <string>
#include <utility>
#include <vector>

#include "flux/core/flux_context.h"
#include "helper/string_utils.h"

namespace XER::flux::core {
namespace {

std::string CanonicalName(std::string_view name) {
	return XER::Engine::Helper::String::CanonicalizeToken(name);
}

}  // namespace

bool ModuleManager::Register(FluxModule module, std::string* error_out) {
	if (XER::Engine::Helper::String::IsBlank(module.name)) {
		if (error_out != nullptr) {
			*error_out = "Flux module name must not be blank";
		}
		return false;
	}
	if (HasModule(module.name)) {
		if (error_out != nullptr) {
			*error_out = "Flux module already registered: " + module.name;
		}
		return false;
	}
	module.name = CanonicalName(module.name);
	modules_.push_back(std::move(module));
	return true;
}

bool ModuleManager::HasModule(std::string_view name) const {
	const std::string canonical = CanonicalName(name);
	for (const FluxModule& module : modules_) {
		if (module.name == canonical) {
			return true;
		}
	}
	return false;
}

std::vector<std::string> ModuleManager::ModuleNames() const {
	std::vector<std::string> names;
	names.reserve(modules_.size());
	for (const FluxModule& module : modules_) {
		names.push_back(module.name);
	}
	return names;
}

bool ModuleManager::StartAll(FluxContext& context, std::string* error_out) {
	started_modules_.clear();
	for (std::size_t index = 0; index < modules_.size(); ++index) {
		FluxModule& module = modules_[index];
		if (module.start && !module.start(context, error_out)) {
			StopAll(context);
			return false;
		}
		started_modules_.push_back(index);
	}
	return true;
}

void ModuleManager::StopAll(FluxContext& context) {
	for (auto iterator = started_modules_.rbegin(); iterator != started_modules_.rend(); ++iterator) {
		FluxModule& module = modules_[*iterator];
		if (module.stop) {
			module.stop(context);
		}
	}
	started_modules_.clear();
}

}  // namespace XER::flux::core
