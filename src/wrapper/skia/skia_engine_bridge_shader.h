#include <vector>
#pragma once
#include <cstdint>

namespace XER::engine::bridge::skia {
    // Create an Image Shader handle
    int ShaderCreateImage(const std::vector<uint32_t>& pixels, int width, int height,
                          bool repeat_x, bool repeat_y);
    // Create a Perlin Noise Shader handle
    int ShaderCreatePerlinNoise(float base_freq_x, float base_freq_y, int num_octaves, float seed);
    // Release shader
    void ShaderRelease(int shader_id);
}
