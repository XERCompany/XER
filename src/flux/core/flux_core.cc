#include "flux/core/flux_core.h"

#include <string>
#include <vector>

#include <absl/strings/str_join.h>

namespace XER::flux::core {
namespace {

std::string BuildStartupMessage(const FluxContext& context, const ModuleManager& module_manager) {
	const std::vector<std::string> modules = module_manager.ModuleNames();
	return BuildConfigSummary(context.config()) + ", modules=[" + absl::StrJoin(modules, ", ") + "]";
}

}  // namespace

FluxCore::FluxCore(FluxConfig config)
	: context_(std::move(config)) {}

FluxContext& FluxCore::context() {
	return context_;
}

const FluxContext& FluxCore::context() const {
	return context_;
}

ModuleManager& FluxCore::module_manager() {
	return module_manager_;
}

const ModuleManager& FluxCore::module_manager() const {
	return module_manager_;
}

bool FluxCore::RegisterModule(FluxModule module, std::string* error_out) {
	return module_manager_.Register(std::move(module), error_out);
}

bool FluxCore::Initialize(std::string* error_out) {
	if (running_) {
		return true;
	}
	context_.RefreshTerminalWindow();
	context_.SetValue("application", context_.config().application_name);
	context_.SetValue("terminal.columns", std::to_string(context_.window().size().columns));
	context_.SetValue("terminal.rows", std::to_string(context_.window().size().rows));
	if (!module_manager_.StartAll(context_, error_out)) {
		context_.logger().Error("core", error_out != nullptr ? *error_out : std::string("Flux module startup failed"));
		return false;
	}
	running_ = true;
	context_.logger().Info("core", "FluxCore initialized: " + BuildStartupMessage(context_, module_manager_));
	return true;
}

void FluxCore::Shutdown() {
	if (!running_) {
		return;
	}
	module_manager_.StopAll(context_);
	running_ = false;
	context_.logger().Info("core", "FluxCore shutdown complete");
}

bool FluxCore::is_running() const {
	return running_;
}

}  // namespace XER::flux::core
