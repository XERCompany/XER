#include "flux/core/flux_context.h"

#include <string>
#include <utility>
#include <vector>

#include "helper/string_utils.h"

namespace XER::flux::core {
namespace {

std::string CanonicalKey(std::string_view key) {
	return XER::Engine::Helper::String::CanonicalizeToken(key);
}

}  // namespace

FluxContext::FluxContext(FluxConfig config)
	: config_(std::move(config)),
	  logger_(config_),
	  window_(config_.terminal_size) {}

FluxConfig& FluxContext::config() {
	return config_;
}

const FluxConfig& FluxContext::config() const {
	return config_;
}

Logger& FluxContext::logger() {
	return logger_;
}

const Logger& FluxContext::logger() const {
	return logger_;
}

XER::flux::terminal::TerminalWindow& FluxContext::window() {
	return window_;
}

const XER::flux::terminal::TerminalWindow& FluxContext::window() const {
	return window_;
}

void FluxContext::RefreshTerminalWindow() {
	window_.RefreshSize();
	config_.terminal_size = window_.size();
	logger_.UpdateConfig(config_);
}

void FluxContext::SetValue(std::string key, std::string value) {
	values_[CanonicalKey(key)] = std::move(value);
}

bool FluxContext::HasValue(std::string_view key) const {
	return values_.find(CanonicalKey(key)) != values_.end();
}

std::string FluxContext::GetValue(std::string_view key) const {
	const auto iterator = values_.find(CanonicalKey(key));
	return iterator == values_.end() ? std::string() : iterator->second;
}

std::vector<std::string> FluxContext::Keys() const {
	std::vector<std::string> keys;
	keys.reserve(values_.size());
	for (const auto& entry : values_) {
		keys.push_back(entry.first);
	}
	return keys;
}

}  // namespace XER::flux::core
