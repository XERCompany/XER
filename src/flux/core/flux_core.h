#pragma once

#include <string>

#include "flux/core/flux_context.h"
#include "flux/core/module_manager.h"

namespace XER::flux::core {

class FluxCore {
public:
	explicit FluxCore(FluxConfig config = FluxConfigFromEnvironment());

	FluxContext& context();
	const FluxContext& context() const;

	ModuleManager& module_manager();
	const ModuleManager& module_manager() const;

	bool RegisterModule(FluxModule module, std::string* error_out = nullptr);
	bool Initialize(std::string* error_out = nullptr);
	void Shutdown();

	bool is_running() const;

private:
	FluxContext context_;
	ModuleManager module_manager_;
	bool running_ = false;
};

}  // namespace XER::flux::core
