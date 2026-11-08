#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace XER::Bridge::Skia {
    // Load a Lottie animation from JSON. Returns a handle.
    int LottieLoadAnimation(const std::string& json_content);
    // Get animation duration in seconds
    double LottieGetDuration(int anim_id);
    // Render a specific time/frame of the animation to the pixel buffer
    bool LottieRenderFrame(int anim_id, std::vector<uint32_t>* pixels, int width, int height,
                           double time_seconds, float x, float y, float scale);
    // Release the animation
    void LottieRelease(int anim_id);
}
