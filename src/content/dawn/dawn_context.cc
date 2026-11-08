#include "dawn_context.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

namespace XER::Content::Dawn {
namespace {

// Generous timeout for adapter/device/map requests. Requires the
// TimedWaitAny instance feature (enabled in Initialize).
constexpr uint64_t kRequestTimeoutNs = 30'000'000'000ull;  // 30 s

}  // namespace

DawnContext::DawnContext() = default;

DawnContext::~DawnContext() {
	// Destroy in reverse dependency order; handles are ref-counted.
	queue_ = wgpu::Queue{};
	device_ = wgpu::Device{};
	adapter_ = wgpu::Adapter{};
	instance_ = wgpu::Instance{};
	surface_ = wgpu::Surface{};
}

void DawnContext::SetError(std::string* out, const std::string& msg) {
	std::lock_guard lock(error_mutex_);
	last_error_ = msg;
	if (out) *out = msg;
}

bool DawnContext::WaitFuture(wgpu::Future future, uint64_t timeout_ns,
			     std::string* error_out) {
	if (!instance_ || future.id == 0) {
		SetError(error_out, "no instance or future");
		return false;
	}
	const wgpu::WaitStatus status = instance_.WaitAny(future, timeout_ns);
	if (status != wgpu::WaitStatus::Success) {
		SetError(error_out, "WaitAny failed (status=" +
					 std::to_string(static_cast<int>(status)) + ")");
		return false;
	}
	return true;
}

bool DawnContext::Initialize(std::string* error_out) {
	if (ready_) return true;

	// TimedWaitAny enables WaitAny with a real timeout (0 would only
	// poll once; nonzero requires this feature).
	const wgpu::InstanceFeatureName kFeatures[] = {
	    wgpu::InstanceFeatureName::TimedWaitAny,
	};
	wgpu::InstanceDescriptor instance_desc{};
	instance_desc.requiredFeatureCount = 1;
	instance_desc.requiredFeatures = kFeatures;
	instance_ = wgpu::CreateInstance(&instance_desc);
	if (!instance_) {
		SetError(error_out, "wgpu::CreateInstance failed");
		return false;
	}

	// --- adapter ---------------------------------------------------------
	struct AdapterResult {
		wgpu::RequestAdapterStatus status = wgpu::RequestAdapterStatus::Error;
		wgpu::Adapter adapter;
		std::string message;
	};
	wgpu::RequestAdapterOptions adapter_opts{};
	AdapterResult adapter_res;
	wgpu::Future adapter_fut = instance_.RequestAdapter(
	    &adapter_opts, wgpu::CallbackMode::AllowSpontaneous,
	    [&adapter_res](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter,
			   wgpu::StringView message) {
		    adapter_res.status = status;
		    adapter_res.adapter = adapter;
		    if (message.data && message.length > 0) {
			    adapter_res.message.assign(message.data, message.length);
		    }
	    });
	if (!WaitFuture(adapter_fut, kRequestTimeoutNs, error_out)) return false;
	if (adapter_res.status != wgpu::RequestAdapterStatus::Success ||
	    !adapter_res.adapter) {
		SetError(error_out, "no WebGPU adapter: " + adapter_res.message);
		return false;
	}
	adapter_ = adapter_res.adapter;

	// --- device ----------------------------------------------------------
	struct DeviceResult {
		wgpu::RequestDeviceStatus status = wgpu::RequestDeviceStatus::Error;
		wgpu::Device device;
		std::string message;
	};
	wgpu::DeviceDescriptor dev_desc{};
	// Callbacks are configured on the device descriptor before request.
	dev_desc.SetUncapturedErrorCallback(
	    [](wgpu::Device const&, wgpu::ErrorType type, wgpu::StringView message) {
		std::fprintf(stderr, "[dawn] uncaptured error (type=%d): %.*s\n",
			     static_cast<int>(type),
			     message.length == WGPU_STRLEN
				 ? static_cast<int>(std::strlen(message.data))
				 : static_cast<int>(message.length),
			     message.data ? message.data : "");
	    });
	dev_desc.SetDeviceLostCallback(
	    wgpu::CallbackMode::AllowSpontaneous,
	    [](wgpu::Device const&, wgpu::DeviceLostReason reason,
	       wgpu::StringView message) {
		std::fprintf(stderr, "[dawn] device lost (reason=%d): %.*s\n",
			     static_cast<int>(reason),
			     message.length == WGPU_STRLEN
				 ? static_cast<int>(std::strlen(message.data))
				 : static_cast<int>(message.length),
			     message.data ? message.data : "");
	    });

	DeviceResult device_res;
	wgpu::Future device_fut = adapter_.RequestDevice(
	    &dev_desc, wgpu::CallbackMode::AllowSpontaneous,
	    [&device_res](wgpu::RequestDeviceStatus status, wgpu::Device device,
			  wgpu::StringView message) {
		    device_res.status = status;
		    device_res.device = device;
		    if (message.data && message.length > 0) {
			    device_res.message.assign(message.data, message.length);
		    }
	    });
	if (!WaitFuture(device_fut, kRequestTimeoutNs, error_out)) return false;
	if (device_res.status != wgpu::RequestDeviceStatus::Success ||
	    !device_res.device) {
		SetError(error_out, "no WebGPU device: " + device_res.message);
		return false;
	}
	device_ = device_res.device;
	queue_ = device_.GetQueue();

	ready_ = true;
	return true;
}

bool DawnContext::CreateWin32Surface(void* hwnd, void* hinstance,
				     std::string* error_out) {
	if (!instance_) {
		SetError(error_out, "instance not initialized");
		return false;
	}
	if (!hwnd) {
		SetError(error_out, "null hwnd");
		return false;
	}
	surface_ = wgpu::Surface{};

	wgpu::SurfaceSourceWindowsHWND surface_src;
	surface_src.hinstance = hinstance;
	surface_src.hwnd = hwnd;

	wgpu::SurfaceDescriptor desc{};
	desc.nextInChain = &surface_src;
	surface_ = instance_.CreateSurface(&desc);
	if (!surface_) {
		SetError(error_out, "CreateSurface failed for HWND");
		return false;
	}
	return true;
}

std::string DawnContext::adapter_info() const {
	if (!adapter_) return {};
	wgpu::AdapterInfo info{};
	if (adapter_.GetInfo(&info) != wgpu::Status::Success) return {};
	auto view_to_string = [](const wgpu::StringView& v) -> std::string {
		if (!v.data) return {};
		if (v.length == WGPU_STRLEN) return std::string(v.data);
		return std::string(v.data, v.length);
	};
	std::string result = view_to_string(info.device);
	const std::string desc = view_to_string(info.description);
	if (!desc.empty()) {
		if (!result.empty()) result += ' ';
		result += desc;
	}
	return result;
}

}  // namespace XER::Content::Dawn
