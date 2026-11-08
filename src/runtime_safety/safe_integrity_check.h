#pragma once

#include <filesystem>
#include <string>

namespace XER::RuntimeSafety {

struct StartupOptions {
	std::filesystem::path script_path;
	bool sandbox = false;
	bool verbose = false;
	int async_io_workers = -1;
	int async_io_queue_depth = -1;
};

struct StartupState {
	std::filesystem::path cache_directory;
	std::size_t async_io_workers = 0;
	std::size_t async_io_queue_depth = 0;
	bool sandbox_enabled = false;
};

bool BootstrapStartup(const StartupOptions& options, StartupState* state, std::string* error_out);

}  // namespace XER::RuntimeSafety
