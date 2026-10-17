#include "gpu_info.h"

#if defined(XER_ASYNC_IO_HAS_CUDA)
#include <cuda_runtime.h>
#endif

#if defined(XER_ASYNC_IO_HAS_VULKAN)
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <utility>

namespace AsyncIO::IO::Hardware {

GPUInfoProvider::GPUInfoProvider()
    : current_device_(0), initialized_(false), supports_cuda_(false), 
    supports_opencl_(false), supports_hip_(false), supports_vulkan_(false) {}

GPUInfoProvider::~GPUInfoProvider() {}

bool GPUInfoProvider::Initialize() {
    DetectGPUs();
    initialized_ = true;
    return gpu_devices_.size() > 0;
}

bool GPUInfoProvider::IsInitialized() const {
    return initialized_;
}

int GPUInfoProvider::GetDeviceCount() const {
    return static_cast<int>(gpu_devices_.size());
}

std::vector<GPUDevice> GPUInfoProvider::GetDevices() const {
    return gpu_devices_;
}

std::string GPUInfoProvider::GetDeviceName(int device_id) const {
    if (device_id >= 0 && device_id < static_cast<int>(gpu_devices_.size())) {
        return gpu_devices_[device_id].name;
    }
    return "";
}

uint64_t GPUInfoProvider::GetTotalMemory(int device_id) const {
    if (device_id >= 0 && device_id < static_cast<int>(gpu_devices_.size())) {
        return gpu_devices_[device_id].total_memory_mb;
    }
    return 0;
}

uint64_t GPUInfoProvider::GetFreeMemory(int device_id) const {
    if (device_id >= 0 && device_id < static_cast<int>(gpu_devices_.size())) {
        return gpu_devices_[device_id].free_memory_mb;
    }
    return 0;
}

bool GPUInfoProvider::SetCurrentDevice(int device_id) {
    if (device_id >= 0 && device_id < static_cast<int>(gpu_devices_.size())) {
        current_device_ = device_id;
        return true;
    }
    return false;
}

int GPUInfoProvider::GetCurrentDevice() const {
    return current_device_;
}

std::string GPUInfoProvider::GetDeviceDescription(int device_id) const {
    if (device_id >= 0 && device_id < static_cast<int>(gpu_devices_.size())) {
        const auto& dev = gpu_devices_[device_id];
        std::string description = dev.name + " (" + std::to_string(dev.total_memory_mb) + "MB)";
        if (!dev.backend.empty()) description += " [" + dev.backend + "]";
        if (!dev.driver_version.empty()) description += " driver " + dev.driver_version;
        return description;
    }
    return "";
}

void GPUInfoProvider::DetectGPUs() {
    DetectVulkanDevices();
    DetectCUDADevices();
}

void GPUInfoProvider::DetectVulkanDevices() {
#if defined(XER_ASYNC_IO_HAS_VULKAN)
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "XER";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "XER";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;

#if defined(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)
    uint32_t extension_count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr) == VK_SUCCESS) {
        std::vector<VkExtensionProperties> extensions(extension_count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions.data()) == VK_SUCCESS) {
            const auto portability_extension = std::find_if(extensions.begin(), extensions.end(),
                [](const VkExtensionProperties& extension) {
                    return std::string(extension.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
                });
            if (portability_extension != extensions.end()) {
                const char* enabled_extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
                create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
                create_info.enabledExtensionCount = 1;
                create_info.ppEnabledExtensionNames = &enabled_extension;
            }
        }
    }
#endif

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) return;

    uint32_t device_count = 0;
    if (vkEnumeratePhysicalDevices(instance, &device_count, nullptr) != VK_SUCCESS || device_count == 0) {
        vkDestroyInstance(instance, nullptr);
        return;
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    if (vkEnumeratePhysicalDevices(instance, &device_count, devices.data()) == VK_SUCCESS) {
        for (uint32_t index = 0; index < device_count; ++index) {
            VkPhysicalDeviceProperties properties{};
            VkPhysicalDeviceMemoryProperties memory_properties{};
            vkGetPhysicalDeviceProperties(devices[index], &properties);
            vkGetPhysicalDeviceMemoryProperties(devices[index], &memory_properties);

            uint64_t local_memory_bytes = 0;
            for (uint32_t heap_index = 0; heap_index < memory_properties.memoryHeapCount; ++heap_index) {
                const auto& heap = memory_properties.memoryHeaps[heap_index];
                if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                    local_memory_bytes += heap.size;
                }
            }

            GPUDevice device{};
            device.device_id = static_cast<int>(index);
            device.name = properties.deviceName;
            device.total_memory_mb = local_memory_bytes / (1024 * 1024);
            device.free_memory_mb = 0;
            device.is_available = true;
            device.vendor_id = properties.vendorID;
            device.pci_device_id = properties.deviceID;
            device.driver_version = std::to_string(properties.driverVersion);
            device.backend = "Vulkan";
            gpu_devices_.push_back(std::move(device));
        }
        supports_vulkan_ = true;
    }

    vkDestroyInstance(instance, nullptr);
#endif
}

void GPUInfoProvider::DetectCUDADevices() {
#if defined(XER_ASYNC_IO_HAS_CUDA)
    int device_count = 0;
    if (cudaGetDeviceCount(&device_count) == cudaSuccess) {
        supports_cuda_ = true;
        int driver_version = 0;
        cudaDriverGetVersion(&driver_version);
        
        for (int i = 0; i < device_count; ++i) {
            cudaDeviceProp props;
            if (cudaGetDeviceProperties(&props, i) == cudaSuccess) {
                GPUDevice dev;
                dev.device_id = i;
                dev.name = props.name;
                dev.compute_capability = std::to_string(props.major) + "." + 
                                        std::to_string(props.minor);
                dev.total_memory_mb = props.totalGlobalMem / (1024 * 1024);
                dev.free_memory_mb = dev.total_memory_mb;  // Simplified
                dev.is_available = true;
                dev.vendor_id = 0x10DE;
                dev.pci_device_id = static_cast<uint32_t>(props.pciDeviceID);
                dev.driver_version = std::to_string(driver_version);
                dev.backend = "CUDA";

                const auto existing = std::find_if(gpu_devices_.begin(), gpu_devices_.end(),
                    [&dev](const GPUDevice& current) {
                        return current.name == dev.name && current.total_memory_mb == dev.total_memory_mb;
                    });
                if (existing == gpu_devices_.end()) {
                    gpu_devices_.push_back(std::move(dev));
                } else {
                    existing->compute_capability = dev.compute_capability;
                    existing->backend += " + CUDA";
                }
            }
        }
    }
#endif
}

}  // namespace AsyncIO::IO::Hardware
