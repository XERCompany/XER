#ifndef XER_UI_CORE_APPLICATION_H
#define XER_UI_CORE_APPLICATION_H

#include <memory>
#include <string>

namespace XER::UI::Core {

/**
 * Application - Main entry point for UI application
 *
 * Deep module with small interface:
 * - Initialize(): Setup platform and internal state
 * - Run(): Start event loop
 * - Shutdown(): Cleanup resources
 *
 * All complexity (window management, event loop, platform init) hidden in implementation.
 */
class Application {
public:
    static Application& Instance();

    // Small interface - just 3 public methods
    bool Initialize();
    int Run();
    void Shutdown();

    // Configuration
    void SetAppName(const std::string& name);
    std::string GetAppName() const;

    // Remote rendering (xer_engine_ui_render.exe worker pool).
    // Call before Initialize to change the pre-spawned worker count
    // (default 2; 0 disables process-separated rendering;
    // XER_UI_RENDER_WORKERS env overrides: N / "auto" / "0").
    void SetRenderWorkerCount(int count);
    // Live worker processes / idle pool workers (-1 when unsupported).
    int RenderWorkerCount() const;
    int WarmRenderWorkerCount() const;

    bool IsInitialized() const;
    bool IsRunning() const;

    // Prevent copying
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

private:
    Application();
    ~Application();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace XER::UI::Core

#endif // XER_UI_CORE_APPLICATION_H
