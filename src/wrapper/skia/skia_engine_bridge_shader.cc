#include "wrapper/skia/skia_engine_bridge_shader.h"

#if ENGINE_HAS_SKIA_BRIDGE
#include "include/core/SkShader.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkPerlinNoiseShader.h"

#include <unordered_map>
#include <mutex>
#include <memory>
#endif

namespace XER::Bridge::Skia {

#if ENGINE_HAS_SKIA_BRIDGE
    
    struct ShaderSession {
        sk_sp<SkShader> shader;
    };
    
    static std::unordered_map<int, std::unique_ptr<ShaderSession>> g_shader_sessions;
    static int g_shader_next_id = 1;
    static std::mutex g_shader_mutex;

    int ShaderCreateImage(const std::vector<uint32_t>& pixels, int width, int height,
                          bool repeat_x, bool repeat_y) {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        
        if (pixels.size() < (size_t)(width * height)) return -1;
        
        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
        SkPixmap pixmap(info, pixels.data(), width * sizeof(uint32_t));
        
        sk_sp<SkImage> image = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
        if (!image) return -1;
        
        SkTileMode tmx = repeat_x ? SkTileMode::kRepeat : SkTileMode::kDecal;
        SkTileMode tmy = repeat_y ? SkTileMode::kRepeat : SkTileMode::kDecal;
        
        sk_sp<SkShader> shader = image->makeShader(tmx, tmy, SkSamplingOptions());
        if (!shader) return -1;

        auto session = std::make_unique<ShaderSession>();
        session->shader = std::move(shader);
        
        int id = g_shader_next_id++;
        g_shader_sessions[id] = std::move(session);
        return id;
    }

    int ShaderCreatePerlinNoise(float base_freq_x, float base_freq_y, int num_octaves, float seed) {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        
        sk_sp<SkShader> shader = SkShaders::MakeFractalNoise(
            base_freq_x, base_freq_y, num_octaves, seed, nullptr);
            
        if (!shader) return -1;
        
        auto session = std::make_unique<ShaderSession>();
        session->shader = std::move(shader);
        
        int id = g_shader_next_id++;
        g_shader_sessions[id] = std::move(session);
        return id;
    }

    void ShaderRelease(int shader_id) {
        std::lock_guard<std::mutex> lock(g_shader_mutex);
        g_shader_sessions.erase(shader_id);
    }
#else
    int ShaderCreateImage(const std::vector<uint32_t>& pixels, int width, int height,
                          bool repeat_x, bool repeat_y) { return -1; }
    int ShaderCreatePerlinNoise(float base_freq_x, float base_freq_y, int num_octaves, float seed) { return -1; }
    void ShaderRelease(int shader_id) {}
#endif

}
