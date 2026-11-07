#include "wrapper/skia/skia_engine_bridge_image_filter.h"
#include <cstdint>

#if ENGINE_HAS_SKIA_BRIDGE
#include "include/core/SkImageFilter.h"
#include "include/effects/SkImageFilters.h"

#include <unordered_map>
#include <mutex>
#include <memory>
#endif

namespace XER::engine::bridge::skia {

#if ENGINE_HAS_SKIA_BRIDGE
    
    struct FilterSession {
        sk_sp<SkImageFilter> filter;
    };
    
    static std::unordered_map<int, std::unique_ptr<FilterSession>> g_filter_sessions;
    static int g_filter_next_id = 1;
    static std::mutex g_filter_mutex;

    int ImageFilterCreateDropShadow(float dx, float dy, float sigma_x, float sigma_y, uint32_t color) {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        
        sk_sp<SkImageFilter> filter = SkImageFilters::DropShadow(
            dx, dy, sigma_x, sigma_y, color, nullptr, nullptr);
            
        if (!filter) return -1;
        
        auto session = std::make_unique<FilterSession>();
        session->filter = std::move(filter);
        
        int id = g_filter_next_id++;
        g_filter_sessions[id] = std::move(session);
        return id;
    }

    int ImageFilterCreateBlur(float sigma_x, float sigma_y) {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        
        sk_sp<SkImageFilter> filter = SkImageFilters::Blur(
            sigma_x, sigma_y, SkTileMode::kDecal, nullptr);
            
        if (!filter) return -1;
        
        auto session = std::make_unique<FilterSession>();
        session->filter = std::move(filter);
        
        int id = g_filter_next_id++;
        g_filter_sessions[id] = std::move(session);
        return id;
    }

    void ImageFilterRelease(int filter_id) {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        g_filter_sessions.erase(filter_id);
    }
#else
    int ImageFilterCreateDropShadow(float dx, float dy, float sigma_x, float sigma_y, uint32_t color) { return -1; }
    int ImageFilterCreateBlur(float sigma_x, float sigma_y) { return -1; }
    void ImageFilterRelease(int filter_id) {}
#endif

}
