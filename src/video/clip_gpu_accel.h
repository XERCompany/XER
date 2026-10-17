#pragma once

#include <string>

namespace video {

class Frame;

struct ClipGpuPostprocessOptions {
    float bass = 0.0f;
    float high = 0.0f;
    float rms = 0.0f;
};

struct ClipGpuPostprocessResult {
    bool opengl_enabled = false;
    bool opencl_enabled = false;
    bool opencl_used = false;
    bool vulkan_enabled = false;
    bool vulkan_used = false;
    std::string backend;
};

bool ClipOpenGlEnabled();
bool ClipOpenClEnabled();
bool ClipVulkanEnabled();
const char* ClipOpenGlPreviewFragmentShader();
ClipGpuPostprocessResult ApplyClipGpuPostprocess(Frame& frame, const ClipGpuPostprocessOptions& options);

}  // namespace video