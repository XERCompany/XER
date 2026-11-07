#pragma once

#include <string_view>

#include "flux/core/flux_config.h"

namespace XER::flux::core {

class Logger {
public:
	explicit Logger(FluxConfig config = {});

	void UpdateConfig(const FluxConfig& config);
	const FluxConfig& config() const;

	bool ShouldLog(LogLevel level) const;
	void Log(LogLevel level, std::string_view component, std::string_view message) const;

	void Debug(std::string_view component, std::string_view message) const;
	void Info(std::string_view component, std::string_view message) const;
	void Warning(std::string_view component, std::string_view message) const;
	void Error(std::string_view component, std::string_view message) const;

private:
	FluxConfig config_;
};

}  // namespace XER::flux::core
