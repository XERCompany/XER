#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace XER::flux::core {

class FluxContext;

struct FluxModule {
	std::string name;
	std::function<bool(FluxContext&, std::string* error_out)> start;
	std::function<void(FluxContext&)> stop;
};

class ModuleManager {
public:
	bool Register(FluxModule module, std::string* error_out = nullptr);
	bool HasModule(std::string_view name) const;
	std::vector<std::string> ModuleNames() const;

	bool StartAll(FluxContext& context, std::string* error_out = nullptr);
	void StopAll(FluxContext& context);

private:
	std::vector<FluxModule> modules_;
	std::vector<std::size_t> started_modules_;
};

}  // namespace XER::flux::core
