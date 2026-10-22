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
