#pragma once

namespace video {

class Frame;

struct RotatingCubeSceneOptions {
    double time_seconds = 0.0;
    float bass = 0.0f;
    float low_mid = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float rms = 0.0f;
};

bool RotatingCubeSceneUsesAssimp();
void RenderRotatingCubeScene(Frame& frame, const RotatingCubeSceneOptions& options);

}  // namespace video