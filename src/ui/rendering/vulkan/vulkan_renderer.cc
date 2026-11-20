// The header pulls <windows.h>; the entire TU is Windows-only (POSIX
// builds get an empty translation unit, matching the /rendering/windows/
// filter convention). Keep the include inside the guard.
#if defined(_WIN32)

#include "rendering/vulkan/vulkan_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rendering/vulkan/embedded_shaders.h"
#include "rendering/windows/raster_util.h"

// --- Vulkan headers -----------------------------------------------------
// The Vulkan SDK is optional for building XER. Probe order:
//   1. <vulkan/vulkan.h>           — Vulkan SDK / packaged headers;
//   2. <third_party/vulkan/...>    — the full header set vendored inside
//      the skia include tree (present wherever the skia port is, which is
//      a hard dependency of XER_UI_Core);
//   3. <skia/include/third_party/...> — same set via the toolchain-root
//      include path (MSYS2 clang64 layout).
// When none resolve, the TU still compiles: XER_UI_VULKAN_HEADERS=0 and
// Create() reports the backend as unavailable.
#if defined(__has_include)
#  if __has_include(<vulkan/vulkan.h>)
#    define VK_USE_PLATFORM_WIN32_KHR 1
#    include <vulkan/vulkan.h>
#    define XER_UI_VULKAN_HEADERS 1
#  elif __has_include(<third_party/vulkan/vulkan/vulkan.h>)
#    define VK_USE_PLATFORM_WIN32_KHR 1
#    include <third_party/vulkan/vulkan/vulkan.h>
#    define XER_UI_VULKAN_HEADERS 1
#  elif __has_include(<skia/include/third_party/vulkan/vulkan/vulkan.h>)
#    define VK_USE_PLATFORM_WIN32_KHR 1
#    include <skia/include/third_party/vulkan/vulkan/vulkan.h>
#    define XER_UI_VULKAN_HEADERS 1
#  endif
#endif
#ifndef XER_UI_VULKAN_HEADERS
#define XER_UI_VULKAN_HEADERS 0
#endif

namespace XER::UI::Rendering::Vulkan {

#if XER_UI_VULKAN_HEADERS

namespace {

// Every Vulkan entry point used below, loaded dynamically so the binary
// runs on machines without vulkan-1.dll (Create then fails early).
struct VkApi {
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance CreateInstance = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties
        EnumerateInstanceExtensionProperties = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties GetPhysicalDeviceProperties =
        nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties
        GetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties
        GetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR
        GetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR
        GetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR
        GetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkCreateWin32SurfaceKHR CreateWin32SurfaceKHR = nullptr;
    PFN_vkDestroySurfaceKHR DestroySurfaceKHR = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties
        EnumerateDeviceExtensionProperties = nullptr;
    PFN_vkCreateDevice CreateDevice = nullptr;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = nullptr;

    PFN_vkDestroyDevice DestroyDevice = nullptr;
    PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
    PFN_vkDeviceWaitIdle DeviceWaitIdle = nullptr;
    PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
    PFN_vkCreateImageView CreateImageView = nullptr;
    PFN_vkDestroyImageView DestroyImageView = nullptr;
    PFN_vkCreateRenderPass CreateRenderPass = nullptr;
    PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer CreateFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;
    PFN_vkCreateCommandPool CreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;
    PFN_vkCmdSetViewport CmdSetViewport = nullptr;
    PFN_vkCmdSetScissor CmdSetScissor = nullptr;
    PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
    PFN_vkCmdPushConstants CmdPushConstants = nullptr;
    PFN_vkCmdDraw CmdDraw = nullptr;
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
    PFN_vkCreateSemaphore CreateSemaphore = nullptr;
    PFN_vkDestroySemaphore DestroySemaphore = nullptr;
    PFN_vkCreateFence CreateFence = nullptr;
    PFN_vkDestroyFence DestroyFence = nullptr;
    PFN_vkWaitForFences WaitForFences = nullptr;
    PFN_vkResetFences ResetFences = nullptr;
    PFN_vkQueueSubmit QueueSubmit = nullptr;
    PFN_vkCreateShaderModule CreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;
    PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline DestroyPipeline = nullptr;
    PFN_vkCreateBuffer CreateBuffer = nullptr;
    PFN_vkDestroyBuffer DestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements =
        nullptr;
    PFN_vkAllocateMemory AllocateMemory = nullptr;
    PFN_vkFreeMemory FreeMemory = nullptr;
    PFN_vkBindBufferMemory BindBufferMemory = nullptr;
    PFN_vkMapMemory MapMemory = nullptr;
    PFN_vkUnmapMemory UnmapMemory = nullptr;
    PFN_vkCreateImage CreateImage = nullptr;
    PFN_vkDestroyImage DestroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements =
        nullptr;
    PFN_vkBindImageMemory BindImageMemory = nullptr;
    PFN_vkCreateSampler CreateSampler = nullptr;
    PFN_vkDestroySampler DestroySampler = nullptr;
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout =
        nullptr;
    PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
    PFN_vkFreeDescriptorSets FreeDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;
};

struct GpuTexture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
};

// Deferred destruction: anything whose lifetime might still be referenced
// by the previous frame's submitted command buffer is queued here and
// released in BeginFrame after the in-flight fence is waited on.
struct Garbage {
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkDeviceMemory> memories;
    std::vector<VkBuffer> buffers;
    std::vector<VkDescriptorSet> sets;
};

constexpr std::size_t kMaxPushBytes = 64;  // 4 x vec4

} // namespace

struct VulkanRenderer::Impl {
    HWND hwnd = nullptr;
    HMODULE loader = nullptr;
    VkApi vk{};

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties mem_props{};
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VkSurfaceFormatKHR surface_format{};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{0, 0};
    std::vector<VkImage> swap_images;
    std::vector<VkImageView> swap_views;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass render_pass = VK_NULL_HANDLE;

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence in_flight = VK_NULL_HANDLE;

    VkShaderModule vs_module = VK_NULL_HANDLE;
    VkShaderModule fs_color = VK_NULL_HANDLE;
    VkShaderModule fs_tex = VK_NULL_HANDLE;
    VkShaderModule fs_mask = VK_NULL_HANDLE;
    VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
    VkPipeline pipe_color = VK_NULL_HANDLE;
    VkPipeline pipe_tex = VK_NULL_HANDLE;
    VkPipeline pipe_mask = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    std::uint32_t image_index = 0;
    bool frame_started = false;
    bool need_recreate = false;
    unsigned dpi = USER_DEFAULT_SCREEN_DPI;
    Theme::Color clear_color{1.0f, 1.0f, 1.0f, 1.0f};
    Layout::Rect bounds_dip{};

    struct Mat2x3 {
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
        float tx = 0.0f, ty = 0.0f;
    } transform{};

    std::vector<VkRect2D> clip_stack;
    std::vector<float> layer_opacity;
    Garbage garbage;

    std::unordered_map<std::string, GpuTexture> image_cache;
    std::unordered_map<std::uint32_t, GpuTexture> resource_cache;
    std::unordered_map<std::uint32_t, GpuTexture> video_cache;
    std::unordered_map<std::string, GpuTexture> text_cache;
    GpuTexture frame_texture;
};

namespace {

// XER_VK_G/I/D(api, handle, Member) — resolves the "vk##Member" entry
// point into api.Member (struct members omit the vk prefix).
#define XER_VK_G(api, member)                                                 \
    api.member = reinterpret_cast<PFN_vk##member>(                            \
        api.GetInstanceProcAddr(VK_NULL_HANDLE, "vk" #member))
#define XER_VK_I(api, inst, member)                                           \
    api.member = reinterpret_cast<PFN_vk##member>(                            \
        api.GetInstanceProcAddr(inst, "vk" #member))
#define XER_VK_D(api, dev, member)                                            \
    api.member = reinterpret_cast<PFN_vk##member>(                            \
        api.GetDeviceProcAddr(dev, "vk" #member))

bool LoadGlobalFunctions(VkApi& vk, std::string* err) {
    XER_VK_G(vk, CreateInstance);
    XER_VK_G(vk, EnumerateInstanceExtensionProperties);
    if (vk.CreateInstance == nullptr) {
        if (err) *err = "vulkan-1.dll exports no vkCreateInstance";
        return false;
    }
    return true;
}

bool LoadInstanceFunctions(VkApi& vk, VkInstance instance) {
    XER_VK_I(vk, instance, DestroyInstance);
    XER_VK_I(vk, instance, EnumeratePhysicalDevices);
    XER_VK_I(vk, instance, GetPhysicalDeviceProperties);
    XER_VK_I(vk, instance, GetPhysicalDeviceQueueFamilyProperties);
    XER_VK_I(vk, instance, GetPhysicalDeviceMemoryProperties);
    XER_VK_I(vk, instance, GetPhysicalDeviceSurfaceSupportKHR);
    XER_VK_I(vk, instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    XER_VK_I(vk, instance, GetPhysicalDeviceSurfaceFormatsKHR);
    XER_VK_I(vk, instance, CreateWin32SurfaceKHR);
    XER_VK_I(vk, instance, DestroySurfaceKHR);
    XER_VK_I(vk, instance, EnumerateDeviceExtensionProperties);
    XER_VK_I(vk, instance, CreateDevice);
    XER_VK_I(vk, instance, GetDeviceProcAddr);
    return vk.DestroyInstance && vk.EnumeratePhysicalDevices &&
           vk.GetPhysicalDeviceQueueFamilyProperties &&
           vk.GetPhysicalDeviceMemoryProperties &&
           vk.GetPhysicalDeviceSurfaceSupportKHR &&
           vk.GetPhysicalDeviceSurfaceCapabilitiesKHR &&
           vk.GetPhysicalDeviceSurfaceFormatsKHR &&
           vk.CreateWin32SurfaceKHR && vk.DestroySurfaceKHR &&
           vk.CreateDevice && vk.GetDeviceProcAddr;
}

bool LoadDeviceFunctions(VkApi& vk, VkDevice device) {
    XER_VK_D(vk, device, DestroyDevice);
    XER_VK_D(vk, device, GetDeviceQueue);
    XER_VK_D(vk, device, DeviceWaitIdle);
    XER_VK_D(vk, device, CreateSwapchainKHR);
    XER_VK_D(vk, device, DestroySwapchainKHR);
    XER_VK_D(vk, device, GetSwapchainImagesKHR);
    XER_VK_D(vk, device, AcquireNextImageKHR);
    XER_VK_D(vk, device, QueuePresentKHR);
    XER_VK_D(vk, device, CreateImageView);
    XER_VK_D(vk, device, DestroyImageView);
    XER_VK_D(vk, device, CreateRenderPass);
    XER_VK_D(vk, device, DestroyRenderPass);
    XER_VK_D(vk, device, CreateFramebuffer);
    XER_VK_D(vk, device, DestroyFramebuffer);
    XER_VK_D(vk, device, CreateCommandPool);
    XER_VK_D(vk, device, DestroyCommandPool);
    XER_VK_D(vk, device, AllocateCommandBuffers);
    XER_VK_D(vk, device, BeginCommandBuffer);
    XER_VK_D(vk, device, EndCommandBuffer);
    XER_VK_D(vk, device, ResetCommandBuffer);
    XER_VK_D(vk, device, CmdBeginRenderPass);
    XER_VK_D(vk, device, CmdEndRenderPass);
    XER_VK_D(vk, device, CmdSetViewport);
    XER_VK_D(vk, device, CmdSetScissor);
    XER_VK_D(vk, device, CmdBindPipeline);
    XER_VK_D(vk, device, CmdBindDescriptorSets);
    XER_VK_D(vk, device, CmdPushConstants);
    XER_VK_D(vk, device, CmdDraw);
    XER_VK_D(vk, device, CmdPipelineBarrier);
    XER_VK_D(vk, device, CmdCopyBufferToImage);
    XER_VK_D(vk, device, CreateSemaphore);
    XER_VK_D(vk, device, DestroySemaphore);
    XER_VK_D(vk, device, CreateFence);
    XER_VK_D(vk, device, DestroyFence);
    XER_VK_D(vk, device, WaitForFences);
    XER_VK_D(vk, device, ResetFences);
    XER_VK_D(vk, device, QueueSubmit);
    XER_VK_D(vk, device, CreateShaderModule);
    XER_VK_D(vk, device, DestroyShaderModule);
    XER_VK_D(vk, device, CreatePipelineLayout);
    XER_VK_D(vk, device, DestroyPipelineLayout);
    XER_VK_D(vk, device, CreateGraphicsPipelines);
    XER_VK_D(vk, device, DestroyPipeline);
    XER_VK_D(vk, device, CreateBuffer);
    XER_VK_D(vk, device, DestroyBuffer);
    XER_VK_D(vk, device, GetBufferMemoryRequirements);
    XER_VK_D(vk, device, AllocateMemory);
    XER_VK_D(vk, device, FreeMemory);
    XER_VK_D(vk, device, BindBufferMemory);
    XER_VK_D(vk, device, MapMemory);
    XER_VK_D(vk, device, UnmapMemory);
    XER_VK_D(vk, device, CreateImage);
    XER_VK_D(vk, device, DestroyImage);
    XER_VK_D(vk, device, GetImageMemoryRequirements);
    XER_VK_D(vk, device, BindImageMemory);
    XER_VK_D(vk, device, CreateSampler);
    XER_VK_D(vk, device, DestroySampler);
    XER_VK_D(vk, device, CreateDescriptorSetLayout);
    XER_VK_D(vk, device, DestroyDescriptorSetLayout);
    XER_VK_D(vk, device, CreateDescriptorPool);
    XER_VK_D(vk, device, DestroyDescriptorPool);
    XER_VK_D(vk, device, AllocateDescriptorSets);
    XER_VK_D(vk, device, FreeDescriptorSets);
    XER_VK_D(vk, device, UpdateDescriptorSets);
    return vk.DestroyDevice && vk.GetDeviceQueue && vk.CreateSwapchainKHR &&
           vk.AcquireNextImageKHR && vk.QueuePresentKHR &&
           vk.CreateGraphicsPipelines && vk.QueueSubmit && vk.CmdDraw;
}

#undef XER_VK_G
#undef XER_VK_I
#undef XER_VK_D

bool HasExtension(const std::vector<VkExtensionProperties>& props,
                  const char* name) {
    for (const auto& p : props) {
        if (std::strcmp(p.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

std::uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& props,
                             std::uint32_t type_bits,
                             VkMemoryPropertyFlags flags) {
    for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

VkShaderModule MakeShaderModule(const VkApi& vk, VkDevice device,
                                const unsigned char* spv,
                                unsigned int len) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = len;
    ci.pCode = reinterpret_cast<const std::uint32_t*>(spv);
    VkShaderModule m = VK_NULL_HANDLE;
    if (vk.CreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return m;
}

void ReleaseTexture(VulkanRenderer::Impl& impl, GpuTexture& tex) {
    if (tex.set != VK_NULL_HANDLE) {
        impl.garbage.sets.push_back(tex.set);
    }
    if (tex.view != VK_NULL_HANDLE) {
        impl.garbage.views.push_back(tex.view);
    }
    if (tex.image != VK_NULL_HANDLE) {
        impl.garbage.images.push_back(tex.image);
    }
    if (tex.memory != VK_NULL_HANDLE) {
        impl.garbage.memories.push_back(tex.memory);
    }
    tex = GpuTexture{};
}

void DrainGarbage(VulkanRenderer::Impl& impl) {
    const VkApi& vk = impl.vk;
    for (VkDescriptorSet s : impl.garbage.sets) {
        vk.FreeDescriptorSets(impl.device, impl.desc_pool, 1, &s);
    }
    for (VkImageView v : impl.garbage.views) {
        vk.DestroyImageView(impl.device, v, nullptr);
    }
    for (VkImage i : impl.garbage.images) {
        vk.DestroyImage(impl.device, i, nullptr);
    }
    for (VkBuffer b : impl.garbage.buffers) {
        vk.DestroyBuffer(impl.device, b, nullptr);
    }
    for (VkDeviceMemory m : impl.garbage.memories) {
        vk.FreeMemory(impl.device, m, nullptr);
    }
    impl.garbage = Garbage{};
}

bool CreateSwapchainObjects(VulkanRenderer::Impl& impl,
                            std::uint32_t width, std::uint32_t height,
                            std::string* err) {
    const VkApi& vk = impl.vk;
    VkSurfaceCapabilitiesKHR caps{};
    if (vk.GetPhysicalDeviceSurfaceCapabilitiesKHR(impl.physical,
                                                   impl.surface,
                                                   &caps) != VK_SUCCESS) {
        if (err) *err = "vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed";
        return false;
    }
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(width, caps.minImageExtent.width,
                                  caps.maxImageExtent.width);
        extent.height = std::clamp(height, caps.minImageExtent.height,
                                   caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        if (err) *err = "zero-size surface (minimized?)";
        return false;
    }
    const std::uint32_t image_count =
        caps.maxImageCount > 0
            ? std::min(caps.minImageCount + 1, caps.maxImageCount)
            : caps.minImageCount + 1;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = impl.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = impl.surface_format.format;
    sci.imageColorSpace = impl.surface_format.colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always supported
    sci.clipped = VK_TRUE;
    if (vk.CreateSwapchainKHR(impl.device, &sci, nullptr,
                              &impl.swapchain) != VK_SUCCESS) {
        if (err) *err = "vkCreateSwapchainKHR failed";
        return false;
    }
    impl.extent = extent;

    std::uint32_t count = 0;
    vk.GetSwapchainImagesKHR(impl.device, impl.swapchain, &count, nullptr);
    impl.swap_images.resize(count);
    vk.GetSwapchainImagesKHR(impl.device, impl.swapchain, &count,
                             impl.swap_images.data());

    impl.swap_views.resize(count);
    impl.framebuffers.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = impl.swap_images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = impl.surface_format.format;
        vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vk.CreateImageView(impl.device, &vci, nullptr,
                               &impl.swap_views[i]) != VK_SUCCESS) {
            if (err) *err = "vkCreateImageView(swapchain) failed";
            return false;
        }
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = impl.render_pass;
        fci.attachmentCount = 1;
        fci.pAttachments = &impl.swap_views[i];
        fci.width = extent.width;
        fci.height = extent.height;
        fci.layers = 1;
        if (vk.CreateFramebuffer(impl.device, &fci, nullptr,
                                 &impl.framebuffers[i]) != VK_SUCCESS) {
            if (err) *err = "vkCreateFramebuffer failed";
            return false;
        }
    }
    return true;
}

void DestroySwapchainObjects(VulkanRenderer::Impl& impl) {
    const VkApi& vk = impl.vk;
    for (VkFramebuffer fb : impl.framebuffers) {
        vk.DestroyFramebuffer(impl.device, fb, nullptr);
    }
    for (VkImageView v : impl.swap_views) {
        vk.DestroyImageView(impl.device, v, nullptr);
    }
    impl.framebuffers.clear();
    impl.swap_views.clear();
    impl.swap_images.clear();
    if (impl.swapchain != VK_NULL_HANDLE) {
        vk.DestroySwapchainKHR(impl.device, impl.swapchain, nullptr);
        impl.swapchain = VK_NULL_HANDLE;
    }
}

// Uploads `w`x`h` BGRA pixels into a fresh device-local image and returns
// a fully described texture (image + view + descriptor set). The upload
// commands record into the active frame's command buffer — callers must
// only invoke this between BeginFrame/EndFrame.
bool UploadTexture(VulkanRenderer::Impl& impl, const void* bgra,
                   std::uint32_t w, std::uint32_t h, GpuTexture* out) {
    const VkApi& vk = impl.vk;
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(w) * h * 4u;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vk.CreateBuffer(impl.device, &bci, nullptr, &staging) !=
        VK_SUCCESS) {
        return false;
    }
    VkMemoryRequirements req{};
    vk.GetBufferMemoryRequirements(impl.device, staging, &req);
    std::uint32_t type = FindMemoryType(
        impl.mem_props, req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type == UINT32_MAX) {
        type = FindMemoryType(impl.mem_props, req.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    if (type == UINT32_MAX) {
        vk.DestroyBuffer(impl.device, staging, nullptr);
        return false;
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (vk.AllocateMemory(impl.device, &mai, nullptr, &staging_mem) !=
        VK_SUCCESS) {
        vk.DestroyBuffer(impl.device, staging, nullptr);
        return false;
    }
    vk.BindBufferMemory(impl.device, staging, staging_mem, 0);
    void* mapped = nullptr;
    if (vk.MapMemory(impl.device, staging_mem, 0, bytes, 0, &mapped) ==
        VK_SUCCESS) {
        std::memcpy(mapped, bgra, static_cast<std::size_t>(bytes));
        vk.UnmapMemory(impl.device, staging_mem);
    }

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vk.CreateImage(impl.device, &ici, nullptr, &out->image) !=
        VK_SUCCESS) {
        impl.garbage.buffers.push_back(staging);
        impl.garbage.memories.push_back(staging_mem);
        return false;
    }
    vk.GetImageMemoryRequirements(impl.device, out->image, &req);
    type = FindMemoryType(impl.mem_props, req.memoryTypeBits,
                          VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type == UINT32_MAX) {
        type = FindMemoryType(impl.mem_props, req.memoryTypeBits,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = type;
    if (type == UINT32_MAX ||
        vk.AllocateMemory(impl.device, &mai, nullptr, &out->memory) !=
            VK_SUCCESS ||
        vk.BindImageMemory(impl.device, out->image, out->memory, 0) !=
            VK_SUCCESS) {
        impl.garbage.buffers.push_back(staging);
        impl.garbage.memories.push_back(staging_mem);
        ReleaseTexture(impl, *out);
        return false;
    }

    // Record: UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST ->
    // SHADER_READ_ONLY — all inside the frame's command buffer.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = out->image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vk.CmdPipelineBarrier(impl.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                          nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vk.CmdCopyBufferToImage(impl.cmd, staging, out->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                            &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vk.CmdPipelineBarrier(impl.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                          nullptr, 0, nullptr, 1, &barrier);

    // Staging dies once this frame's submit completes.
    impl.garbage.buffers.push_back(staging);
    impl.garbage.memories.push_back(staging_mem);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = out->image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_B8G8R8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vk.CreateImageView(impl.device, &vci, nullptr, &out->view) !=
        VK_SUCCESS) {
        ReleaseTexture(impl, *out);
        return false;
    }
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = impl.desc_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &impl.desc_layout;
    if (vk.AllocateDescriptorSets(impl.device, &dsai, &out->set) !=
        VK_SUCCESS) {
        ReleaseTexture(impl, *out);
        return false;
    }
    VkDescriptorImageInfo dii{};
    dii.sampler = impl.sampler;
    dii.imageView = out->view;
    dii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = out->set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &dii;
    vk.UpdateDescriptorSets(impl.device, 1, &write, 0, nullptr);
    out->w = w;
    out->h = h;
    return true;
}

} // namespace

// ---------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------

VulkanRenderer::VulkanRenderer(HWND hwnd)
    : impl_(std::make_unique<Impl>()) {
    impl_->hwnd = hwnd;
    impl_->dpi =
        hwnd != nullptr ? GetDpiForWindow(hwnd) : USER_DEFAULT_SCREEN_DPI;
    if (impl_->dpi == 0) {
        impl_->dpi = USER_DEFAULT_SCREEN_DPI;
    }
}

std::unique_ptr<VulkanRenderer> VulkanRenderer::Create(
    HWND hwnd, std::string* error_out) {
    auto fail = [&](const char* msg) -> std::unique_ptr<VulkanRenderer> {
        if (error_out) {
            *error_out = msg;
        }
        return nullptr;
    };
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return fail("Vulkan renderer requires a valid HWND");
    }

    auto renderer = std::unique_ptr<VulkanRenderer>(
        new VulkanRenderer(hwnd));
    Impl& impl = *renderer->impl_;
    VkApi& vk = impl.vk;
    std::string err;

    // --- loader + global entry points --------------------------------
    impl.loader = LoadLibraryW(L"vulkan-1.dll");
    if (impl.loader == nullptr) {
        return fail("vulkan-1.dll not present (no Vulkan runtime)");
    }
    vk.GetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(impl.loader, "vkGetInstanceProcAddr"));
    if (vk.GetInstanceProcAddr == nullptr ||
        !LoadGlobalFunctions(vk, &err)) {
        return fail(err.empty() ? "vkGetInstanceProcAddr missing"
                                : err.c_str());
    }

    // --- instance -----------------------------------------------------
    std::uint32_t ext_count = 0;
    vk.EnumerateInstanceExtensionProperties(nullptr, &ext_count,
                                            nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    if (ext_count > 0) {
        vk.EnumerateInstanceExtensionProperties(nullptr, &ext_count,
                                                exts.data());
    }
    if (!HasExtension(exts, VK_KHR_SURFACE_EXTENSION_NAME) ||
        !HasExtension(exts, VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
        return fail("loader exposes no Win32 surface extension (no ICD)");
    }
    const char* inst_exts[] = {VK_KHR_SURFACE_EXTENSION_NAME,
                               VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "xer_ui";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = 2;
    ici.ppEnabledExtensionNames = inst_exts;
    if (vk.CreateInstance(&ici, nullptr, &impl.instance) != VK_SUCCESS) {
        return fail("vkCreateInstance failed (no usable ICD)");
    }
    if (!LoadInstanceFunctions(vk, impl.instance)) {
        return fail("incomplete instance-level Vulkan entry points");
    }

    // --- WSI surface ---------------------------------------------------
    VkWin32SurfaceCreateInfoKHR wci{};
    wci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    wci.hinstance = GetModuleHandleW(nullptr);
    wci.hwnd = hwnd;
    if (vk.CreateWin32SurfaceKHR(impl.instance, &wci, nullptr,
                                 &impl.surface) != VK_SUCCESS) {
        return fail("vkCreateWin32SurfaceKHR failed");
    }

    // --- physical device + queue family --------------------------------
    std::uint32_t dev_count = 0;
    vk.EnumeratePhysicalDevices(impl.instance, &dev_count, nullptr);
    if (dev_count == 0) {
        return fail("no Vulkan physical devices (driver has no ICD)");
    }
    std::vector<VkPhysicalDevice> devices(dev_count);
    vk.EnumeratePhysicalDevices(impl.instance, &dev_count,
                                devices.data());
    bool found = false;
    for (VkPhysicalDevice pd : devices) {
        std::uint32_t qf_count = 0;
        vk.GetPhysicalDeviceQueueFamilyProperties(pd, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qf_count);
        vk.GetPhysicalDeviceQueueFamilyProperties(pd, &qf_count,
                                                  qf.data());
        for (std::uint32_t i = 0; i < qf_count; ++i) {
            if (!(qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                continue;
            }
            VkBool32 present = VK_FALSE;
            vk.GetPhysicalDeviceSurfaceSupportKHR(pd, i, impl.surface,
                                                  &present);
            if (present) {
                impl.physical = pd;
                impl.queue_family = i;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    if (!found) {
        return fail("no graphics+present queue family");
    }

    std::uint32_t dext_count = 0;
    vk.EnumerateDeviceExtensionProperties(impl.physical, nullptr,
                                          &dext_count, nullptr);
    std::vector<VkExtensionProperties> dexts(dext_count);
    if (dext_count > 0) {
        vk.EnumerateDeviceExtensionProperties(impl.physical, nullptr,
                                              &dext_count, dexts.data());
    }
    if (!HasExtension(dexts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        return fail("device lacks VK_KHR_swapchain");
    }
    vk.GetPhysicalDeviceMemoryProperties(impl.physical,
                                         &impl.mem_props);

    // Surface format: prefer BGRA8 UNORM (matches DXGI/D2D path).
    std::uint32_t fmt_count = 0;
    vk.GetPhysicalDeviceSurfaceFormatsKHR(impl.physical, impl.surface,
                                          &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    if (fmt_count > 0) {
        vk.GetPhysicalDeviceSurfaceFormatsKHR(
            impl.physical, impl.surface, &fmt_count, formats.data());
    }
    impl.surface_format = {VK_FORMAT_B8G8R8A8_UNORM,
                           VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            impl.surface_format = f;
            break;
        }
        if (f.format != VK_FORMAT_UNDEFINED) {
            impl.surface_format = f;
        }
    }

    // --- logical device -------------------------------------------------
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = impl.queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = dev_exts;
    if (vk.CreateDevice(impl.physical, &dci, nullptr, &impl.device) !=
        VK_SUCCESS) {
        return fail("vkCreateDevice failed");
    }
    if (!LoadDeviceFunctions(vk, impl.device)) {
        return fail("incomplete device-level Vulkan entry points");
    }
    vk.GetDeviceQueue(impl.device, impl.queue_family, 0, &impl.queue);

    // --- render pass -----------------------------------------------------
    VkAttachmentDescription attachment{};
    attachment.format = impl.surface_format.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &attachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    if (vk.CreateRenderPass(impl.device, &rpci, nullptr,
                          &impl.render_pass) != VK_SUCCESS) {
        return fail("vkCreateRenderPass failed");
    }

    // --- initial swapchain ----------------------------------------------
    RECT client{};
    GetClientRect(hwnd, &client);
    if (!CreateSwapchainObjects(
            impl, static_cast<std::uint32_t>(std::max<LONG>(1, client.right)),
            static_cast<std::uint32_t>(std::max<LONG>(1, client.bottom)),
            &err)) {
        return fail(err.c_str());
    }

    // --- shaders / descriptor layout / pipelines -------------------------
    impl.vs_module = MakeShaderModule(vk, impl.device,
                                      Shaders::kQuadVertSpv,
                                      Shaders::kQuadVertSpvLen);
    impl.fs_color = MakeShaderModule(vk, impl.device,
                                     Shaders::kQuadColorFragSpv,
                                     Shaders::kQuadColorFragSpvLen);
    impl.fs_tex = MakeShaderModule(vk, impl.device,
                                   Shaders::kQuadTexFragSpv,
                                   Shaders::kQuadTexFragSpvLen);
    impl.fs_mask = MakeShaderModule(vk, impl.device,
                                    Shaders::kQuadMaskFragSpv,
                                    Shaders::kQuadMaskFragSpvLen);
    if (!impl.vs_module || !impl.fs_color || !impl.fs_tex ||
        !impl.fs_mask) {
        return fail("vkCreateShaderModule failed");
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dslci{};
    dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslci.bindingCount = 1;
    dslci.pBindings = &binding;
    if (vk.CreateDescriptorSetLayout(impl.device, &dslci, nullptr,
                                     &impl.desc_layout) != VK_SUCCESS) {
        return fail("vkCreateDescriptorSetLayout failed");
    }
    VkDescriptorPoolSize pool_size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   256};
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets = 256;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &pool_size;
    if (vk.CreateDescriptorPool(impl.device, &dpci, nullptr,
                                &impl.desc_pool) != VK_SUCCESS) {
        return fail("vkCreateDescriptorPool failed");
    }

    VkPushConstantRange push_range{};
    push_range.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push_range.offset = 0;
    push_range.size = kMaxPushBytes;
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &impl.desc_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &push_range;
    if (vk.CreatePipelineLayout(impl.device, &plci, nullptr,
                                &impl.pipe_layout) != VK_SUCCESS) {
        return fail("vkCreatePipelineLayout failed");
    }

    VkSamplerCreateInfo sci2{};
    sci2.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci2.magFilter = VK_FILTER_LINEAR;
    sci2.minFilter = VK_FILTER_LINEAR;
    sci2.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci2.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci2.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vk.CreateSampler(impl.device, &sci2, nullptr, &impl.sampler) !=
        VK_SUCCESS) {
        return fail("vkCreateSampler failed");
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = impl.vs_module;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineVertexInputStateCreateInfo vin{};
    vin.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.blendEnable = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor =
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.alphaBlendOp = VK_BLEND_OP_ADD;
    blend_att.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &blend_att;
    const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                         VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vin;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.pDynamicState = &dyn;
    gpci.layout = impl.pipe_layout;
    gpci.renderPass = impl.render_pass;
    gpci.subpass = 0;

    stages[1].module = impl.fs_color;
    if (vk.CreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &gpci,
                                   nullptr,
                                   &impl.pipe_color) != VK_SUCCESS) {
        return fail("vkCreateGraphicsPipelines(color) failed");
    }
    stages[1].module = impl.fs_tex;
    if (vk.CreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &gpci,
                                   nullptr,
                                   &impl.pipe_tex) != VK_SUCCESS) {
        return fail("vkCreateGraphicsPipelines(tex) failed");
    }
    stages[1].module = impl.fs_mask;
    if (vk.CreateGraphicsPipelines(impl.device, VK_NULL_HANDLE, 1, &gpci,
                                   nullptr,
                                   &impl.pipe_mask) != VK_SUCCESS) {
        return fail("vkCreateGraphicsPipelines(mask) failed");
    }

    // --- command pool + sync ---------------------------------------------
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = impl.queue_family;
    if (vk.CreateCommandPool(impl.device, &cpci, nullptr,
                             &impl.cmd_pool) != VK_SUCCESS) {
        return fail("vkCreateCommandPool failed");
    }
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = impl.cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    if (vk.AllocateCommandBuffers(impl.device, &cbai, &impl.cmd) !=
        VK_SUCCESS) {
        return fail("vkAllocateCommandBuffers failed");
    }
    VkSemaphoreCreateInfo sci3{};
    sci3.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vk.CreateSemaphore(impl.device, &sci3, nullptr,
                           &impl.image_available) != VK_SUCCESS ||
        vk.CreateSemaphore(impl.device, &sci3, nullptr,
                           &impl.render_finished) != VK_SUCCESS ||
        vk.CreateFence(impl.device, &fci, nullptr, &impl.in_flight) !=
            VK_SUCCESS) {
        return fail("sync object creation failed");
    }
    return renderer;
}

VulkanRenderer::~VulkanRenderer() {
    if (impl_ == nullptr) {
        return;
    }
    Impl& impl = *impl_;
    const VkApi& vk = impl.vk;
    if (impl.device != VK_NULL_HANDLE) {
        vk.DeviceWaitIdle(impl.device);
        DrainGarbage(impl);

        for (auto& [k, t] : impl.image_cache) ReleaseTexture(impl, t);
        for (auto& [k, t] : impl.resource_cache) ReleaseTexture(impl, t);
        for (auto& [k, t] : impl.video_cache) ReleaseTexture(impl, t);
        for (auto& [k, t] : impl.text_cache) ReleaseTexture(impl, t);
        ReleaseTexture(impl, impl.frame_texture);
        DrainGarbage(impl);

        if (impl.in_flight) vk.DestroyFence(impl.device, impl.in_flight, nullptr);
        if (impl.image_available)
            vk.DestroySemaphore(impl.device, impl.image_available, nullptr);
        if (impl.render_finished)
            vk.DestroySemaphore(impl.device, impl.render_finished, nullptr);
        if (impl.cmd_pool)
            vk.DestroyCommandPool(impl.device, impl.cmd_pool, nullptr);
        if (impl.pipe_color) vk.DestroyPipeline(impl.device, impl.pipe_color, nullptr);
        if (impl.pipe_tex) vk.DestroyPipeline(impl.device, impl.pipe_tex, nullptr);
        if (impl.pipe_mask) vk.DestroyPipeline(impl.device, impl.pipe_mask, nullptr);
        if (impl.sampler) vk.DestroySampler(impl.device, impl.sampler, nullptr);
        if (impl.pipe_layout)
            vk.DestroyPipelineLayout(impl.device, impl.pipe_layout, nullptr);
        if (impl.desc_pool)
            vk.DestroyDescriptorPool(impl.device, impl.desc_pool, nullptr);
        if (impl.desc_layout)
            vk.DestroyDescriptorSetLayout(impl.device, impl.desc_layout, nullptr);
        if (impl.vs_module) vk.DestroyShaderModule(impl.device, impl.vs_module, nullptr);
        if (impl.fs_color) vk.DestroyShaderModule(impl.device, impl.fs_color, nullptr);
        if (impl.fs_tex) vk.DestroyShaderModule(impl.device, impl.fs_tex, nullptr);
        if (impl.fs_mask) vk.DestroyShaderModule(impl.device, impl.fs_mask, nullptr);
        DestroySwapchainObjects(impl);
        if (impl.render_pass)
            vk.DestroyRenderPass(impl.device, impl.render_pass, nullptr);
        vk.DestroyDevice(impl.device, nullptr);
    }
    if (impl.surface != VK_NULL_HANDLE) {
        vk.DestroySurfaceKHR(impl.instance, impl.surface, nullptr);
    }
    if (impl.instance != VK_NULL_HANDLE) {
        vk.DestroyInstance(impl.instance, nullptr);
    }
    if (impl.loader != nullptr) {
        FreeLibrary(impl.loader);
    }
}

// ---------------------------------------------------------------------
// frame management
// ---------------------------------------------------------------------

void VulkanRenderer::SetDpi(unsigned dpi) {
    if (dpi == 0) {
        throw std::invalid_argument("DPI must be greater than zero");
    }
    if (impl_->frame_started) {
        throw std::logic_error(
            "Cannot change renderer DPI during an active frame");
    }
    impl_->dpi = dpi;
}

void VulkanRenderer::SetClearColor(Theme::Color color) {
    impl_->clear_color = color;
}

void VulkanRenderer::BeginFrame(Layout::Rect bounds) {
    Impl& impl = *impl_;
    if (impl.frame_started) {
        throw std::logic_error("A Vulkan frame is already active");
    }
    const VkApi& vk = impl.vk;
    impl.bounds_dip = bounds;

    // Wait for the previous frame's submit — after this everything in
    // the garbage list is safe to release.
    vk.WaitForFences(impl.device, 1, &impl.in_flight, VK_TRUE,
                     UINT64_MAX);
    DrainGarbage(impl);

    // Recreate the swapchain on resize / OUT_OF_DATE.
    RECT client{};
    GetClientRect(impl.hwnd, &client);
    const std::uint32_t w =
        static_cast<std::uint32_t>(std::max<LONG>(1, client.right));
    const std::uint32_t h =
        static_cast<std::uint32_t>(std::max<LONG>(1, client.bottom));
    if (impl.need_recreate || w != impl.extent.width ||
        h != impl.extent.height) {
        vk.DeviceWaitIdle(impl.device);
        DestroySwapchainObjects(impl);
        std::string err;
        if (!CreateSwapchainObjects(impl, w, h, &err)) {
            throw std::runtime_error("Vulkan swapchain: " + err);
        }
        impl.need_recreate = false;
    }

    VkResult acq = vk.AcquireNextImageKHR(
        impl.device, impl.swapchain, UINT64_MAX, impl.image_available,
        VK_NULL_HANDLE, &impl.image_index);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        impl.need_recreate = true;
        // Rebuild once and retry — a minimized window reports a
        // zero extent and fails CreateSwapchainObjects; that throw is
        // the correct surface-loss behavior.
        vk.DeviceWaitIdle(impl.device);
        DestroySwapchainObjects(impl);
        std::string err;
        if (!CreateSwapchainObjects(impl, w, h, &err)) {
            throw std::runtime_error("Vulkan swapchain: " + err);
        }
        impl.need_recreate = false;
        acq = vk.AcquireNextImageKHR(impl.device, impl.swapchain,
                                     UINT64_MAX, impl.image_available,
                                     VK_NULL_HANDLE, &impl.image_index);
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }
    if (acq == VK_SUBOPTIMAL_KHR) {
        impl.need_recreate = true;
    }

    vk.ResetFences(impl.device, 1, &impl.in_flight);
    vk.ResetCommandBuffer(impl.cmd, 0);
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vk.BeginCommandBuffer(impl.cmd, &begin) != VK_SUCCESS) {
        throw std::runtime_error("vkBeginCommandBuffer failed");
    }

    VkClearValue clear{};
    clear.color.float32[0] = std::clamp(impl.clear_color.r, 0.0f, 1.0f);
    clear.color.float32[1] = std::clamp(impl.clear_color.g, 0.0f, 1.0f);
    clear.color.float32[2] = std::clamp(impl.clear_color.b, 0.0f, 1.0f);
    clear.color.float32[3] = std::clamp(impl.clear_color.a, 0.0f, 1.0f);
    VkRenderPassBeginInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = impl.render_pass;
    rp.framebuffer = impl.framebuffers[impl.image_index];
    rp.renderArea = {{0, 0}, impl.extent};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vk.CmdBeginRenderPass(impl.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(impl.extent.width);
    viewport.height = static_cast<float>(impl.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vk.CmdSetViewport(impl.cmd, 0, 1, &viewport);
    impl.clip_stack.clear();
    impl.layer_opacity.clear();
    impl.transform = Impl::Mat2x3{};
    const VkRect2D full{{0, 0}, impl.extent};
    vk.CmdSetScissor(impl.cmd, 0, 1, &full);
    impl.frame_started = true;
}

void VulkanRenderer::EndFrame() {
    Impl& impl = *impl_;
    if (!impl.frame_started) {
        throw std::logic_error("Vulkan EndFrame without BeginFrame");
    }
    impl.frame_started = false;
    const VkApi& vk = impl.vk;

    vk.CmdEndRenderPass(impl.cmd);
    if (vk.EndCommandBuffer(impl.cmd) != VK_SUCCESS) {
        throw std::runtime_error("vkEndCommandBuffer failed");
    }
    const VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &impl.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &impl.cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &impl.render_finished;
    if (vk.QueueSubmit(impl.queue, 1, &submit, impl.in_flight) !=
        VK_SUCCESS) {
        throw std::runtime_error("vkQueueSubmit failed");
    }
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &impl.render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &impl.swapchain;
    present.pImageIndices = &impl.image_index;
    const VkResult pr = vk.QueuePresentKHR(impl.queue, &present);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        impl.need_recreate = true;
    } else if (pr != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }
}

// ---------------------------------------------------------------------
// drawing
// ---------------------------------------------------------------------

namespace {

// Computes the NDC quad for a DIP-space rect: DIP -> 2x3 transform ->
// pixels (x dpi/96) -> NDC. Corners emitted TL,TR,BL,BR matching the
// vertex shader's index mapping.
void DipRectToNdc(const VulkanRenderer::Impl& impl, Layout::Rect rect,
                  float* ndc /*8 floats*/) {
    const float s = static_cast<float>(impl.dpi) / 96.0f;
    const float xs[4] = {rect.x, rect.x + rect.width, rect.x,
                         rect.x + rect.width};
    const float ys[4] = {rect.y, rect.y, rect.y + rect.height,
                         rect.y + rect.height};
    const float w = std::max<float>(1.0f, impl.extent.width);
    const float h = std::max<float>(1.0f, impl.extent.height);
    for (int i = 0; i < 4; ++i) {
        const float dx = impl.transform.a * xs[i] +
                         impl.transform.c * ys[i] + impl.transform.tx;
        const float dy = impl.transform.b * xs[i] +
                         impl.transform.d * ys[i] + impl.transform.ty;
        ndc[i * 2] = dx * s * 2.0f / w - 1.0f;
        ndc[i * 2 + 1] = 1.0f - dy * s * 2.0f / h;
    }
}

struct PushBlock {
    float corners01[4];
    float corners23[4];
    float color[4];
    float uv[4];
};

void EmitQuad(VulkanRenderer::Impl& impl, const float ndc[8],
              Theme::Color color, VkPipeline pipeline,
              VkDescriptorSet set) {
    float opacity = 1.0f;
    for (const float o : impl.layer_opacity) {
        opacity *= o;
    }
    const VkApi& vk = impl.vk;
    PushBlock pb{};
    pb.corners01[0] = ndc[0];
    pb.corners01[1] = ndc[1];
    pb.corners01[2] = ndc[2];
    pb.corners01[3] = ndc[3];
    pb.corners23[0] = ndc[4];
    pb.corners23[1] = ndc[5];
    pb.corners23[2] = ndc[6];
    pb.corners23[3] = ndc[7];
    pb.color[0] = std::clamp(color.r, 0.0f, 1.0f);
    pb.color[1] = std::clamp(color.g, 0.0f, 1.0f);
    pb.color[2] = std::clamp(color.b, 0.0f, 1.0f);
    pb.color[3] = std::clamp(color.a * opacity, 0.0f, 1.0f);
    pb.uv[0] = 0.0f;
    pb.uv[1] = 0.0f;
    pb.uv[2] = 1.0f;
    pb.uv[3] = 1.0f;
    vk.CmdBindPipeline(impl.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                       pipeline);
    if (set != VK_NULL_HANDLE) {
        vk.CmdBindDescriptorSets(impl.cmd,
                                 VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 impl.pipe_layout, 0, 1, &set, 0,
                                 nullptr);
    }
    vk.CmdPushConstants(impl.cmd, impl.pipe_layout,
                        VK_SHADER_STAGE_VERTEX_BIT |
                            VK_SHADER_STAGE_FRAGMENT_BIT,
                        0, sizeof(pb), &pb);
    vk.CmdDraw(impl.cmd, 6, 1, 0, 0);
}

void EmitTextureQuad(VulkanRenderer::Impl& impl, const GpuTexture& tex,
                     Layout::Rect dst_dip, float opacity) {
    float ndc[8];
    DipRectToNdc(impl, dst_dip, ndc);
    EmitQuad(impl, ndc, {1.0f, 1.0f, 1.0f, opacity}, impl.pipe_tex,
             tex.set);
}

Layout::Rect NormalizeRect(Layout::Rect rect) {
    rect.width = std::max(0.0f, rect.width);
    rect.height = std::max(0.0f, rect.height);
    return rect;
}

} // namespace

void VulkanRenderer::FillRect(Layout::Rect rect, Theme::Color color,
                              float radius) {
    Impl& impl = *impl_;
    if (!impl.frame_started) {
        throw std::logic_error("Vulkan FillRect requires an active frame");
    }
    (void)radius;  // rounded corners: deferred (SDF pipeline) — header doc
    rect = NormalizeRect(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    float ndc[8];
    DipRectToNdc(impl, rect, ndc);
    EmitQuad(impl, ndc, color, impl.pipe_color, VK_NULL_HANDLE);
}

void VulkanRenderer::StrokeRect(Layout::Rect rect, Theme::Color color,
                                float width, float radius) {
    (void)radius;
    rect = NormalizeRect(rect);
    if (rect.width <= 0.0f || rect.height <= 0.0f || !(width > 0.0f)) {
        return;
    }
    FillRect({rect.x, rect.y, rect.width, width}, color, 0.0f);
    FillRect({rect.x, rect.y + rect.height - width, rect.width, width},
             color, 0.0f);
    const float inner_h = std::max(0.0f, rect.height - 2.0f * width);
    FillRect({rect.x, rect.y + width, width, inner_h}, color, 0.0f);
    FillRect({rect.x + rect.width - width, rect.y + width, width, inner_h},
             color, 0.0f);
}

void VulkanRenderer::DrawLine(Layout::Point start, Layout::Point end,
                              Theme::Color color, float width) {
    Impl& impl = *impl_;
    if (!impl.frame_started || !(width > 0.0f)) {
        return;
    }
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    float nx = 0.0f, ny = 0.0f;
    if (len > 1e-6f) {
        nx = -dy / len;
        ny = dx / len;
    }
    const float hw = width * 0.5f;
    // Corners in TL,TR,BL,BR order relative to the segment direction.
    const float q[8] = {start.x + nx * hw, start.y + ny * hw,
                        end.x + nx * hw,   end.y + ny * hw,
                        start.x - nx * hw, start.y - ny * hw,
                        end.x - nx * hw,   end.y - ny * hw};
    const float s = static_cast<float>(impl.dpi) / 96.0f;
    const float w = std::max<float>(1.0f, impl.extent.width);
    const float h = std::max<float>(1.0f, impl.extent.height);
    float ndc[8];
    for (int i = 0; i < 4; ++i) {
        const float px = q[i * 2], py = q[i * 2 + 1];
        const float tx = impl.transform.a * px + impl.transform.c * py +
                         impl.transform.tx;
        const float ty = impl.transform.b * px + impl.transform.d * py +
                         impl.transform.ty;
        ndc[i * 2] = tx * s * 2.0f / w - 1.0f;
        ndc[i * 2 + 1] = 1.0f - ty * s * 2.0f / h;
    }
    EmitQuad(impl, ndc, color, impl.pipe_color, VK_NULL_HANDLE);
}

void VulkanRenderer::DrawText(std::string_view text, Layout::Rect rect,
                              TextStyle style) {
    Impl& impl = *impl_;
    if (!impl.frame_started) {
        throw std::logic_error("Vulkan DrawText requires an active frame");
    }
    rect = NormalizeRect(rect);
    if (text.empty() || rect.width <= 0.0f || rect.height <= 0.0f) {
        return;
    }
    const float s = static_cast<float>(impl.dpi) / 96.0f;
    const int w_px =
        std::max(1, static_cast<int>(rect.width * s + 0.5f));
    const int h_px =
        std::max(1, static_cast<int>(rect.height * s + 0.5f));
    if (w_px > 4096 || h_px > 4096 ||
        static_cast<std::size_t>(w_px) * h_px > (16u << 20)) {
        return;
    }

    const std::string key =
        std::to_string(w_px) + "x" + std::to_string(h_px) + "|" +
        std::to_string(style.weight) + "|" +
        std::to_string(static_cast<int>(style.size * s + 0.5f)) + "|" +
        std::string(text);
    GpuTexture* tex = nullptr;
    auto it = impl.text_cache.find(key);
    if (it != impl.text_cache.end()) {
        tex = &it->second;
    } else {
        TextStyle px_style = style;
        px_style.size = style.size * s;  // RasterizeMask wants px
        std::vector<std::uint8_t> mask;
        if (!Windows::GdiText::RasterizeMask(text, px_style, w_px, h_px,
                                             &mask)) {
            return;
        }
        if (impl.text_cache.size() >= 128) {
            for (auto& [k, t] : impl.text_cache) {
                ReleaseTexture(impl, t);
            }
            impl.text_cache.clear();
        }
        GpuTexture fresh;
        if (!UploadTexture(impl, mask.data(),
                           static_cast<std::uint32_t>(w_px),
                           static_cast<std::uint32_t>(h_px), &fresh)) {
            return;
        }
        tex = &impl.text_cache.emplace(key, fresh).first->second;
    }
    float ndc[8];
    DipRectToNdc(impl, rect, ndc);
    EmitQuad(impl, ndc, style.color, impl.pipe_mask, tex->set);
}

Layout::Size VulkanRenderer::MeasureText(std::string_view text,
                                         TextStyle style) const {
    return Windows::GdiText::Measure(text, style, impl_->dpi);
}

void VulkanRenderer::PushClip(Layout::Rect rect) {
    Impl& impl = *impl_;
    if (!impl.frame_started) {
        return;
    }
    rect = NormalizeRect(rect);
    // Scissor is integer pixels; take the AABB of the transformed clip
    // (exact for axis-aligned transforms, a superset under rotation).
    float ndc[8];
    DipRectToNdc(impl, rect, ndc);
    const float w = static_cast<float>(impl.extent.width);
    const float h = static_cast<float>(impl.extent.height);
    auto px_x = [&](float x) { return (x + 1.0f) * 0.5f * w; };
    auto px_y = [&](float y) { return (1.0f - y) * 0.5f * h; };
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int i = 0; i < 4; ++i) {
        x0 = std::min(x0, px_x(ndc[i * 2]));
        y0 = std::min(y0, px_y(ndc[i * 2 + 1]));
        x1 = std::max(x1, px_x(ndc[i * 2]));
        y1 = std::max(y1, px_y(ndc[i * 2 + 1]));
    }
    VkRect2D sc{{static_cast<std::int32_t>(std::floor(x0)),
                 static_cast<std::int32_t>(std::floor(y0))},
                {static_cast<std::uint32_t>(std::max(
                     0.0f, std::ceil(x1 - x0))),
                 static_cast<std::uint32_t>(std::max(
                     0.0f, std::ceil(y1 - y0)))}};
    if (!impl.clip_stack.empty()) {
        const VkRect2D& top = impl.clip_stack.back();
        const std::int32_t ix0 =
            std::max(sc.offset.x, top.offset.x);
        const std::int32_t iy0 =
            std::max(sc.offset.y, top.offset.y);
        const std::int32_t ix1 = std::min(
            sc.offset.x + static_cast<std::int32_t>(sc.extent.width),
            top.offset.x + static_cast<std::int32_t>(top.extent.width));
        const std::int32_t iy1 = std::min(
            sc.offset.y + static_cast<std::int32_t>(sc.extent.height),
            top.offset.y +
                static_cast<std::int32_t>(top.extent.height));
        sc.offset = {ix0, iy0};
        sc.extent = {static_cast<std::uint32_t>(
                         std::max<std::int32_t>(0, ix1 - ix0)),
                     static_cast<std::uint32_t>(
                         std::max<std::int32_t>(0, iy1 - iy0))};
    }
    impl.clip_stack.push_back(sc);
    impl.vk.CmdSetScissor(impl.cmd, 0, 1, &sc);
}

void VulkanRenderer::PopClip() {
    Impl& impl = *impl_;
    if (!impl.frame_started || impl.clip_stack.empty()) {
        return;
    }
    impl.clip_stack.pop_back();
    const VkRect2D sc = impl.clip_stack.empty()
                          ? VkRect2D{{0, 0}, impl.extent}
                          : impl.clip_stack.back();
    impl.vk.CmdSetScissor(impl.cmd, 0, 1, &sc);
}

void VulkanRenderer::DrawBackdropBlur(Layout::Rect rect, float radius) {
    // Frosted glass is deferred; approximate with the same translucent
    // scrim the D2D path uses for full-window modal backdrops.
    (void)radius;
    FillRect(rect, {0.0f, 0.0f, 0.0f, 0.45f}, 0.0f);
}

bool VulkanRenderer::DrawImage(std::string_view path, Layout::Rect rect,
                               float opacity, float corner_radius) {
    Impl& impl = *impl_;
    if (!impl.frame_started) {
        return false;
    }
    (void)corner_radius;  // deferred — header doc
    rect = NormalizeRect(rect);
    if (path.empty() || rect.width <= 0.0f || rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }
    const std::string key(path);
    auto it = impl.image_cache.find(key);
    if (it == impl.image_cache.end()) {
        std::vector<std::uint8_t> pixels;
        int w = 0, h = 0;
        GpuTexture tex;
        const bool ok =
            Windows::DecodeImageFileToBgra(key, &pixels, &w, &h) &&
            UploadTexture(impl, pixels.data(),
                          static_cast<std::uint32_t>(w),
                          static_cast<std::uint32_t>(h), &tex);
        if (!ok) {
            return false;  // don't cache failures — decode may succeed later
        }
        if (impl.image_cache.size() >= 64) {
            for (auto& [k, t] : impl.image_cache) {
                ReleaseTexture(impl, t);
            }
            impl.image_cache.clear();
        }
        it = impl.image_cache.emplace(key, tex).first;
    }
    EmitTextureQuad(impl, it->second, rect, opacity);
    return true;
}

bool VulkanRenderer::DrawFrameBgra(const void* bgra, int width, int height,
                                   Layout::Rect dst_rect, float opacity) {
    Impl& impl = *impl_;
    if (!impl.frame_started) {
        return false;
    }
    dst_rect = NormalizeRect(dst_rect);
    if (bgra == nullptr || width <= 0 || height <= 0 ||
        dst_rect.width <= 0.0f || dst_rect.height <= 0.0f ||
        !(opacity > 0.0f)) {
        return false;
    }
    // Live-video path: each frame is a fresh upload; the replaced
    // texture is released via the deferred garbage list once this
    // frame's fence completes.
    GpuTexture fresh;
    if (!UploadTexture(impl, bgra, (std::uint32_t)width,
                       (std::uint32_t)height, &fresh)) {
        return false;
    }
    ReleaseTexture(impl, impl.frame_texture);
    impl.frame_texture = fresh;
    EmitTextureQuad(impl, impl.frame_texture, dst_rect, opacity);
    return true;
}

void VulkanRenderer::PushLayer(float opacity, std::uint32_t blend_mode,
                               float blur) {
    if (impl_->frame_started) {
        (void)blend_mode;  // deferred — same approximation as the D2D path
        (void)blur;
        impl_->layer_opacity.push_back(
            std::clamp(opacity, 0.0f, 1.0f));
    }
}

void VulkanRenderer::PopLayer() {
    if (impl_->frame_started && !impl_->layer_opacity.empty()) {
        impl_->layer_opacity.pop_back();
    }
}

void VulkanRenderer::SetTransform(float a, float b, float c, float d,
                                  float tx, float ty) {
    if (impl_->frame_started) {
        impl_->transform = Impl::Mat2x3{a, b, c, d, tx, ty};
    }
}

void VulkanRenderer::ResetTransform() {
    if (impl_->frame_started) {
        impl_->transform = Impl::Mat2x3{};
    }
}

bool VulkanRenderer::DrawResourcePixels(const void* bgra,
                                        std::uint32_t id, int width,
                                        int height, Layout::Rect dst,
                                        float opacity,
                                        float corner_radius) {
    Impl& impl = *impl_;
    if (!impl.frame_started || bgra == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }
    (void)corner_radius;
    auto it = impl.resource_cache.find(id);
    if (it == impl.resource_cache.end()) {
        GpuTexture tex;
        if (!UploadTexture(impl, bgra, (std::uint32_t)width,
                           (std::uint32_t)height, &tex)) {
            return false;
        }
        if (impl.resource_cache.size() >= 256) {
            for (auto& [k, t] : impl.resource_cache) {
                ReleaseTexture(impl, t);
            }
            impl.resource_cache.clear();
        }
        it = impl.resource_cache.emplace(id, tex).first;
    }
    EmitTextureQuad(impl, it->second, NormalizeRect(dst), opacity);
    return true;
}

bool VulkanRenderer::DrawVideoPixels(const void* bgra, std::uint32_t id,
                                     int width, int height,
                                     Layout::Rect dst, float opacity,
                                     float corner_radius) {
    Impl& impl = *impl_;
    if (!impl.frame_started || bgra == nullptr || width <= 0 ||
        height <= 0) {
        return false;
    }
    (void)corner_radius;
    // Per-id stage texture rewritten every frame — a fresh transient
    // upload replaces the old texture (old one dies via garbage after
    // this frame's fence).
    GpuTexture fresh;
    if (!UploadTexture(impl, bgra, (std::uint32_t)width,
                       (std::uint32_t)height, &fresh)) {
        return false;
    }
    auto& slot = impl.video_cache[id];
    ReleaseTexture(impl, slot);
    slot = fresh;
    EmitTextureQuad(impl, slot, NormalizeRect(dst), opacity);
    return true;
}

void VulkanRenderer::FreeResource(std::uint32_t id) {
    Impl& impl = *impl_;
    if (auto it = impl.resource_cache.find(id);
        it != impl.resource_cache.end()) {
        ReleaseTexture(impl, it->second);
        impl.resource_cache.erase(it);
    }
    if (auto it = impl.video_cache.find(id);
        it != impl.video_cache.end()) {
        ReleaseTexture(impl, it->second);
        impl.video_cache.erase(it);
    }
}

#else // !XER_UI_VULKAN_HEADERS

struct VulkanRenderer::Impl {};

VulkanRenderer::VulkanRenderer(HWND hwnd) : impl_(nullptr) {
    (void)hwnd;
}

std::unique_ptr<VulkanRenderer> VulkanRenderer::Create(
    HWND hwnd, std::string* error_out) {
    (void)hwnd;
    if (error_out) {
        *error_out =
            "built without Vulkan headers (no SDK and no vendored "
            "vulkan/vulkan.h on the include path)";
    }
    return nullptr;
}

VulkanRenderer::~VulkanRenderer() = default;

#define XER_VK_UNREACHABLE_THROW                                              \
    throw std::logic_error("VulkanRenderer unavailable in this build")

void VulkanRenderer::SetDpi(unsigned) {}
void VulkanRenderer::SetClearColor(Theme::Color) {}
void VulkanRenderer::BeginFrame(Layout::Rect) { XER_VK_UNREACHABLE_THROW; }
void VulkanRenderer::EndFrame() { XER_VK_UNREACHABLE_THROW; }
void VulkanRenderer::FillRect(Layout::Rect, Theme::Color, float) {}
void VulkanRenderer::StrokeRect(Layout::Rect, Theme::Color, float, float) {}
void VulkanRenderer::DrawLine(Layout::Point, Layout::Point, Theme::Color,
                              float) {}
void VulkanRenderer::DrawText(std::string_view, Layout::Rect, TextStyle) {}
Layout::Size VulkanRenderer::MeasureText(std::string_view,
                                         TextStyle) const {
    return {};
}
void VulkanRenderer::PushClip(Layout::Rect) {}
void VulkanRenderer::PopClip() {}
void VulkanRenderer::DrawBackdropBlur(Layout::Rect, float) {}
bool VulkanRenderer::DrawImage(std::string_view, Layout::Rect, float,
                               float) {
    return false;
}
bool VulkanRenderer::DrawFrameBgra(const void*, int, int, Layout::Rect,
                                   float) {
    return false;
}
void VulkanRenderer::PushLayer(float, std::uint32_t, float) {}
void VulkanRenderer::PopLayer() {}
void VulkanRenderer::SetTransform(float, float, float, float, float,
                                  float) {}
void VulkanRenderer::ResetTransform() {}
bool VulkanRenderer::DrawResourcePixels(const void*, std::uint32_t, int,
                                        int, Layout::Rect, float, float) {
    return false;
}
bool VulkanRenderer::DrawVideoPixels(const void*, std::uint32_t, int, int,
                                     Layout::Rect, float, float) {
    return false;
}
void VulkanRenderer::FreeResource(std::uint32_t) {}

#undef XER_VK_UNREACHABLE_THROW

#endif // XER_UI_VULKAN_HEADERS

} // namespace XER::UI::Rendering::Vulkan

#endif // _WIN32
