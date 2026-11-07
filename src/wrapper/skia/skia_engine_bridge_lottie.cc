#include "wrapper/skia/skia_engine_bridge_lottie.h"

#if ENGINE_HAS_SKIA_BRIDGE

#include "include/core/SkCanvas.h"
#include "include/core/SkStream.h"
#include "include/core/SkBitmap.h"

#if __has_include("modules/skottie/include/Skottie.h") || __has_include(<modules/skottie/include/Skottie.h>)
#define ENGINE_SKIA_HAS_SKOTTIE 1
#include "modules/skottie/include/Skottie.h"
#endif

#include <unordered_map>
#include <mutex>
#include <memory>
#endif

namespace XER::engine::bridge::skia {

#if ENGINE_HAS_SKIA_BRIDGE && defined(ENGINE_SKIA_HAS_SKOTTIE)

    struct SkottieSession {
        sk_sp<skottie::Animation> animation;
    };
    
    static std::unordered_map<int, std::unique_ptr<SkottieSession>> g_skottie_sessions;
    static int g_skottie_next_id = 1;
    static std::mutex g_skottie_mutex;

    int LottieLoadAnimation(const std::string& json_content) {
        std::lock_guard<std::mutex> lock(g_skottie_mutex);
        
        SkMemoryStream stream(json_content.c_str(), json_content.size(), false);
        sk_sp<skottie::Animation> animation = skottie::Animation::Builder().make(&stream);
        
        if (!animation) {
            return -1;
        }

        auto session = std::make_unique<SkottieSession>();
        session->animation = std::move(animation);
        
        int id = g_skottie_next_id++;
        g_skottie_sessions[id] = std::move(session);
        return id;
    }

    double LottieGetDuration(int anim_id) {
        std::lock_guard<std::mutex> lock(g_skottie_mutex);
        auto it = g_skottie_sessions.find(anim_id);
        if (it != g_skottie_sessions.end() && it->second->animation) {
            return it->second->animation->duration();
        }
        return 0.0;
    }

    bool LottieRenderFrame(int anim_id, std::vector<uint32_t>* pixels, int width, int height,
                           double time_seconds, float x, float y, float scale) {
        std::lock_guard<std::mutex> lock(g_skottie_mutex);
        auto it = g_skottie_sessions.find(anim_id);
        if (it == g_skottie_sessions.end() || !it->second->animation) return false;
        if (!pixels || pixels->size() < (size_t)(width * height)) return false;

        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height);
        SkBitmap bitmap;
        bitmap.installPixels(info, pixels->data(), width * sizeof(uint32_t));
        SkCanvas canvas(bitmap);

        // Normalize time (0.0 to 1.0) based on duration
        double duration = it->second->animation->duration();
        double t_normalized = (duration > 0.0) ? (fmod(time_seconds, duration) / duration) : 0.0;

        it->second->animation->seek(t_normalized);
        
        canvas.save();
        canvas.translate(x, y);
        canvas.scale(scale, scale);
        
        // Let it render into its defined bounding box
        it->second->animation->render(&canvas);
        
        canvas.restore();
        return true;
    }

    void LottieRelease(int anim_id) {
        std::lock_guard<std::mutex> lock(g_skottie_mutex);
        g_skottie_sessions.erase(anim_id);
    }

#else

    int LottieLoadAnimation(const std::string& json_content) { return -1; }
    double LottieGetDuration(int anim_id) { return 0.0; }
    bool LottieRenderFrame(int anim_id, std::vector<uint32_t>* pixels, int width, int height,
                           double time_seconds, float x, float y, float scale) { return false; }
    void LottieRelease(int anim_id) {}

#endif

}
