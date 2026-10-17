#include "clip_gpu_accel.h"

#include "frame.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#if ENGINE_HAS_OPENCL
#ifndef CL_TARGET_OPENCL_VERSION
#define CL_TARGET_OPENCL_VERSION 120
#endif
#include <CL/cl.h>
#endif

#if ENGINE_HAS_VULKAN
#if ENGINE_USES_VOLK
#define VK_NO_PROTOTYPES
#include <volk.h>
#else
#include <vulkan/vulkan.h>
#endif
#endif

namespace video {
namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

void ApplyCpuPostprocess(Frame& frame, const ClipGpuPostprocessOptions& options) {
    if (!frame.IsValid() || frame.Format() != PixelFormat::BGRA8) return;
    const float bass = Clamp01(options.bass);
    const float high = Clamp01(options.high);
    const float rms = Clamp01(options.rms);
    const float contrast = 1.04f + rms * 0.10f;
    const float red_lift = 14.0f + bass * 28.0f;
    const float blue_lift = 12.0f + high * 32.0f;
    const size_t pixel_count = static_cast<size_t>(frame.Width()) * static_cast<size_t>(frame.Height());
    uint8_t* data = frame.Data();
    for (size_t index = 0; index < pixel_count; ++index) {
        uint8_t* pixel = data + index * 4u;
        float b = static_cast<float>(pixel[0]);
        float g = static_cast<float>(pixel[1]);
        float r = static_cast<float>(pixel[2]);
        r = (r - 128.0f) * contrast + 128.0f + red_lift;
        g = (g - 128.0f) * (1.0f + rms * 0.04f) + 128.0f + high * 8.0f;
        b = (b - 128.0f) * contrast + 128.0f + blue_lift;
        pixel[0] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
        pixel[1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
        pixel[2] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
        pixel[3] = 255;
    }
}

#if ENGINE_HAS_OPENCL
const char kOpenClKernelSource[] = R"CLC(
__kernel void darkx_clip_neon_postprocess(__global uchar4* pixels,
                                          const int pixel_count,
                                          const float bass,
                                          const float high,
                                          const float rms) {
    const int id = get_global_id(0);
    if (id >= pixel_count) return;

    uchar4 p = pixels[id];
    const float contrast = 1.04f + rms * 0.10f;
    float b = ((float)p.x - 128.0f) * contrast + 128.0f + 12.0f + high * 32.0f;
    float g = ((float)p.y - 128.0f) * (1.0f + rms * 0.04f) + 128.0f + high * 8.0f;
    float r = ((float)p.z - 128.0f) * contrast + 128.0f + 14.0f + bass * 28.0f;

    p.x = (uchar)clamp(b, 0.0f, 255.0f);
    p.y = (uchar)clamp(g, 0.0f, 255.0f);
    p.z = (uchar)clamp(r, 0.0f, 255.0f);
    p.w = (uchar)255;
    pixels[id] = p;
}
)CLC";

class OpenClPostprocessor {
public:
    OpenClPostprocessor() {
        Initialize();
    }

    ~OpenClPostprocessor() {
        if (kernel_ != nullptr) clReleaseKernel(kernel_);
        if (program_ != nullptr) clReleaseProgram(program_);
        if (queue_ != nullptr) clReleaseCommandQueue(queue_);
        if (context_ != nullptr) clReleaseContext(context_);
    }

    bool Available() const {
        return available_;
    }

    bool Apply(Frame& frame, const ClipGpuPostprocessOptions& options) {
        if (!available_ || !frame.IsValid() || frame.Format() != PixelFormat::BGRA8) return false;
        std::lock_guard<std::mutex> lock(mutex_);

        const int pixel_count = frame.Width() * frame.Height();
        const size_t data_size = frame.DataSize();
        cl_int status = CL_SUCCESS;
        cl_mem buffer = clCreateBuffer(context_, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, data_size, frame.Data(), &status);
        if (status != CL_SUCCESS || buffer == nullptr) return false;

        const float bass = Clamp01(options.bass);
        const float high = Clamp01(options.high);
        const float rms = Clamp01(options.rms);
        status = clSetKernelArg(kernel_, 0, sizeof(cl_mem), &buffer);
        status |= clSetKernelArg(kernel_, 1, sizeof(int), &pixel_count);
        status |= clSetKernelArg(kernel_, 2, sizeof(float), &bass);
        status |= clSetKernelArg(kernel_, 3, sizeof(float), &high);
        status |= clSetKernelArg(kernel_, 4, sizeof(float), &rms);
        if (status != CL_SUCCESS) {
            clReleaseMemObject(buffer);
            return false;
        }

        const size_t global = static_cast<size_t>(pixel_count);
        status = clEnqueueNDRangeKernel(queue_, kernel_, 1, nullptr, &global, nullptr, 0, nullptr, nullptr);
        if (status == CL_SUCCESS) {
            status = clEnqueueReadBuffer(queue_, buffer, CL_TRUE, 0, data_size, frame.Data(), 0, nullptr, nullptr);
        }
        clReleaseMemObject(buffer);
        return status == CL_SUCCESS;
    }

private:
    void Initialize() {
        cl_uint platform_count = 0;
        if (clGetPlatformIDs(0, nullptr, &platform_count) != CL_SUCCESS || platform_count == 0) return;
        std::vector<cl_platform_id> platforms(platform_count);
        if (clGetPlatformIDs(platform_count, platforms.data(), nullptr) != CL_SUCCESS) return;

        for (cl_platform_id platform : platforms) {
            if (TryPlatform(platform, CL_DEVICE_TYPE_GPU) || TryPlatform(platform, CL_DEVICE_TYPE_CPU)) {
                available_ = true;
                return;
            }
        }
    }

    bool TryPlatform(cl_platform_id platform, cl_device_type device_type) {
        cl_uint device_count = 0;
        if (clGetDeviceIDs(platform, device_type, 0, nullptr, &device_count) != CL_SUCCESS || device_count == 0) return false;
        std::vector<cl_device_id> devices(device_count);
        if (clGetDeviceIDs(platform, device_type, device_count, devices.data(), nullptr) != CL_SUCCESS) return false;

        cl_int status = CL_SUCCESS;
        cl_context context = clCreateContext(nullptr, 1, &devices.front(), nullptr, nullptr, &status);
        if (status != CL_SUCCESS || context == nullptr) return false;

        cl_command_queue queue = clCreateCommandQueue(context, devices.front(), 0, &status);
        if (status != CL_SUCCESS || queue == nullptr) {
            clReleaseContext(context);
            return false;
        }

        const char* source = kOpenClKernelSource;
        const size_t source_length = sizeof(kOpenClKernelSource) - 1u;
        cl_program program = clCreateProgramWithSource(context, 1, &source, &source_length, &status);
        if (status != CL_SUCCESS || program == nullptr) {
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            return false;
        }

        status = clBuildProgram(program, 1, &devices.front(), nullptr, nullptr, nullptr);
        if (status != CL_SUCCESS) {
            clReleaseProgram(program);
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            return false;
        }

        cl_kernel kernel = clCreateKernel(program, "darkx_clip_neon_postprocess", &status);
        if (status != CL_SUCCESS || kernel == nullptr) {
            clReleaseProgram(program);
            clReleaseCommandQueue(queue);
            clReleaseContext(context);
            return false;
        }

        context_ = context;
        queue_ = queue;
        program_ = program;
        kernel_ = kernel;
        return true;
    }

    bool available_ = false;
    std::mutex mutex_;
    cl_context context_ = nullptr;
    cl_command_queue queue_ = nullptr;
    cl_program program_ = nullptr;
    cl_kernel kernel_ = nullptr;
};

OpenClPostprocessor& OpenClProcessor() {
    static OpenClPostprocessor processor;
    return processor;
}
#endif

#if ENGINE_HAS_VULKAN
struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

class VulkanFrameAccelerator {
public:
    VulkanFrameAccelerator() {
        Initialize();
    }

    ~VulkanFrameAccelerator() {
        Shutdown();
    }

    bool Available() const {
        return available_;
    }

    bool Apply(Frame& frame) {
        if (!available_ || !frame.IsValid() || frame.Format() != PixelFormat::BGRA8) return false;
        std::lock_guard<std::mutex> lock(mutex_);

        const VkDeviceSize frame_size = static_cast<VkDeviceSize>(frame.DataSize());
        if (!EnsureBuffers(frame_size)) return false;
        std::memcpy(upload_.mapped, frame.Data(), static_cast<size_t>(frame_size));

        if (vkResetCommandBuffer(command_buffer_, 0) != VK_SUCCESS) return false;
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) return false;

        VkBufferCopy region{};
        region.size = frame_size;
        vkCmdCopyBuffer(command_buffer_, upload_.buffer, device_local_.buffer, 1, &region);
        vkCmdCopyBuffer(command_buffer_, device_local_.buffer, download_.buffer, 1, &region);
        if (vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) return false;

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer_;
        if (vkResetFences(device_, 1, &fence_) != VK_SUCCESS) return false;
        if (vkQueueSubmit(queue_, 1, &submit_info, fence_) != VK_SUCCESS) return false;
        if (vkWaitForFences(device_, 1, &fence_, VK_TRUE, 1'000'000'000ull) != VK_SUCCESS) return false;

        std::memcpy(frame.Data(), download_.mapped, static_cast<size_t>(frame_size));
        return true;
    }

private:
    void Initialize() {
#if ENGINE_USES_VOLK
        if (volkInitialize() != VK_SUCCESS) return;
#endif
        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = "DarkX Music Clip Vulkan Accelerator";
        app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.pEngineName = "XER";
        app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        app_info.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo instance_info{};
        instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instance_info.pApplicationInfo = &app_info;
        if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) return;
    #if ENGINE_USES_VOLK
        volkLoadInstance(instance_);
    #endif

        if (!SelectPhysicalDevice()) return;

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{};
        queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = queue_family_index_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        VkDeviceCreateInfo device_info{};
        device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1;
        device_info.pQueueCreateInfos = &queue_info;
        if (vkCreateDevice(physical_device_, &device_info, nullptr, &device_) != VK_SUCCESS) return;
    #if ENGINE_USES_VOLK
        volkLoadDevice(device_);
    #endif
        vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_index_;
        if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) return;

        VkCommandBufferAllocateInfo command_info{};
        command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_info.commandPool = command_pool_;
        command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command_info.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device_, &command_info, &command_buffer_) != VK_SUCCESS) return;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device_, &fence_info, nullptr, &fence_) != VK_SUCCESS) return;

        available_ = true;
    }

    bool SelectPhysicalDevice() {
        uint32_t device_count = 0;
        if (vkEnumeratePhysicalDevices(instance_, &device_count, nullptr) != VK_SUCCESS || device_count == 0) return false;
        std::vector<VkPhysicalDevice> devices(device_count);
        if (vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()) != VK_SUCCESS) return false;

        for (VkPhysicalDevice candidate : devices) {
            uint32_t queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, nullptr);
            if (queue_count == 0) continue;
            std::vector<VkQueueFamilyProperties> queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queue_count, queues.data());
            for (uint32_t index = 0; index < queue_count; ++index) {
                const VkQueueFlags flags = queues[index].queueFlags;
                if ((flags & VK_QUEUE_TRANSFER_BIT) || (flags & VK_QUEUE_COMPUTE_BIT) || (flags & VK_QUEUE_GRAPHICS_BIT)) {
                    physical_device_ = candidate;
                    queue_family_index_ = index;
                    return true;
                }
            }
        }
        return false;
    }

    bool EnsureBuffers(VkDeviceSize requested_size) {
        if (requested_size == 0) return false;
        if (upload_.buffer != VK_NULL_HANDLE && upload_.size >= requested_size) return true;
        DestroyBuffer(upload_);
        DestroyBuffer(device_local_);
        DestroyBuffer(download_);

        const VkDeviceSize capacity = requested_size;
        return CreateBuffer(capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true, &upload_)
            && CreateBuffer(capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &device_local_)
            && CreateBuffer(capacity, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, true, &download_);
    }

    bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, bool map, VulkanBuffer* out) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &buffer_info, nullptr, &out->buffer) != VK_SUCCESS) return false;

        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, out->buffer, &requirements);
        const uint32_t memory_type = FindMemoryType(requirements.memoryTypeBits, properties);
        if (memory_type == UINT32_MAX) {
            DestroyBuffer(*out);
            return false;
        }

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = requirements.size;
        alloc_info.memoryTypeIndex = memory_type;
        if (vkAllocateMemory(device_, &alloc_info, nullptr, &out->memory) != VK_SUCCESS) {
            DestroyBuffer(*out);
            return false;
        }
        if (vkBindBufferMemory(device_, out->buffer, out->memory, 0) != VK_SUCCESS) {
            DestroyBuffer(*out);
            return false;
        }
        out->size = size;
        if (map && vkMapMemory(device_, out->memory, 0, size, 0, &out->mapped) != VK_SUCCESS) {
            DestroyBuffer(*out);
            return false;
        }
        return true;
    }

    uint32_t FindMemoryType(uint32_t type_bits, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memory_properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
        for (uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            if ((type_bits & (1u << index))
                && (memory_properties.memoryTypes[index].propertyFlags & properties) == properties) {
                return index;
            }
        }
        return UINT32_MAX;
    }

    void DestroyBuffer(VulkanBuffer& buffer) {
        if (buffer.mapped != nullptr) {
            vkUnmapMemory(device_, buffer.memory);
            buffer.mapped = nullptr;
        }
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, buffer.buffer, nullptr);
            buffer.buffer = VK_NULL_HANDLE;
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device_, buffer.memory, nullptr);
            buffer.memory = VK_NULL_HANDLE;
        }
        buffer.size = 0;
    }

    void Shutdown() {
        if (device_ != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device_);
            DestroyBuffer(upload_);
            DestroyBuffer(device_local_);
            DestroyBuffer(download_);
            if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
            if (command_pool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, command_pool_, nullptr);
            vkDestroyDevice(device_, nullptr);
        }
        if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
        physical_device_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        queue_ = VK_NULL_HANDLE;
        command_pool_ = VK_NULL_HANDLE;
        command_buffer_ = VK_NULL_HANDLE;
        fence_ = VK_NULL_HANDLE;
        available_ = false;
    }

    std::mutex mutex_;
    bool available_ = false;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_index_ = 0;
    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VulkanBuffer upload_;
    VulkanBuffer device_local_;
    VulkanBuffer download_;
};

VulkanFrameAccelerator& VulkanProcessor() {
    static VulkanFrameAccelerator processor;
    return processor;
}
#endif

}  // namespace

bool ClipOpenGlEnabled() {
#if ENGINE_HAS_OPENGL
    return true;
#else
    return false;
#endif
}

bool ClipOpenClEnabled() {
#if ENGINE_HAS_OPENCL
    return OpenClProcessor().Available();
#else
    return false;
#endif
}

bool ClipVulkanEnabled() {
#if ENGINE_HAS_VULKAN
    return VulkanProcessor().Available();
#else
    return false;
#endif
}

const char* ClipOpenGlPreviewFragmentShader() {
    return R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D frameTexture;
uniform float bass;
uniform float high;
uniform float rms;
void main() {
    vec4 c = texture(frameTexture, vUv);
    c.rgb = (c.rgb - vec3(0.5)) * (1.04 + rms * 0.10) + vec3(0.5);
    c.r += 0.055 + bass * 0.110;
    c.b += 0.047 + high * 0.125;
    fragColor = vec4(clamp(c.rgb, 0.0, 1.0), 1.0);
}
)GLSL";
}

ClipGpuPostprocessResult ApplyClipGpuPostprocess(Frame& frame, const ClipGpuPostprocessOptions& options) {
    ClipGpuPostprocessResult result;
    result.opengl_enabled = ClipOpenGlEnabled();
    result.opencl_enabled = ClipOpenClEnabled();
    result.vulkan_enabled = ClipVulkanEnabled();

#if ENGINE_HAS_VULKAN
    if (result.vulkan_enabled && VulkanProcessor().Apply(frame)) {
        result.vulkan_used = true;
    }
#endif

#if ENGINE_HAS_OPENCL
    if (result.opencl_enabled && OpenClProcessor().Apply(frame, options)) {
        result.opencl_used = true;
        result.backend = result.vulkan_used ? "vulkan+opencl" : "opencl";
        return result;
    }
#endif

    ApplyCpuPostprocess(frame, options);
    if (result.vulkan_used) {
        result.backend = "vulkan+cpu";
    } else {
        result.backend = result.opengl_enabled ? "opengl_shader_cpu_fallback" : "cpu";
    }
    return result;
}

}  // namespace video