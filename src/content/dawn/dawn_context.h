#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include <dawn/webgpu_cpp.h>

namespace XER::Content::Dawn {

// Owns a Dawn (WebGPU) instance, adapter and device.
// All heavy init is synchronous via Instance::WaitAny.
class DawnContext {
public:
	DawnContext();
	~DawnContext();

	DawnContext(const DawnContext&) = delete;
	DawnContext& operator=(const DawnContext&) = delete;

	bool Initialize(std::string* error_out = nullptr);
	bool is_ready() const { return ready_; }

	wgpu::Instance& instance() { return instance_; }
	wgpu::Adapter& adapter() { return adapter_; }
	wgpu::Device& device() { return device_; }
	wgpu::Queue& queue() { return queue_; }

	// Creates a Win32 HWND-backed surface on this context.
	bool CreateWin32Surface(void* hwnd, void* hinstance,
				std::string* error_out = nullptr);
	wgpu::Surface& surface() { return surface_; }
	bool has_surface() const { return surface_ != nullptr; }

	// Blocking wait on a Dawn future (adapter/device/map requests).
	bool WaitFuture(wgpu::Future future, uint64_t timeout_ns,
			std::string* error_out = nullptr);

	std::string adapter_info() const;

private:
	void SetError(std::string* out, const std::string& msg);

	wgpu::Instance instance_;
	wgpu::Adapter adapter_;
	wgpu::Device device_;
	wgpu::Queue queue_;
	wgpu::Surface surface_;
	bool ready_ = false;

	mutable std::mutex error_mutex_;
	std::string last_error_;
};

}  // namespace XER::Content::Dawn
