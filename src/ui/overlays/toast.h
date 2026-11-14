#ifndef XER_UI_OVERLAYS_TOAST_H
#define XER_UI_OVERLAYS_TOAST_H

// Native desktop toast notifications (Win32 layered-window overlay,
// Steam-notification style: dark rounded card, bottom-right stack,
// slide-in + auto-dismiss). Rendered on a dedicated UI thread — Show()
// is safe to call from any thread.

#include <cstdint>
#include <memory>
#include <string>

namespace XER::UI::Overlays {

enum class ToastType { Info, Success, Warning, Error };

struct ToastOptions {
    std::string title;
    std::string message;
    ToastType type = ToastType::Info;
    // Auto-dismiss delay in milliseconds (0 = sticky until clicked).
    int duration_ms = 4200;
    // Optional icon file (.ico/.png/.jpg), drawn left of the text.
    std::string icon_path;
};

class ToastManager {
public:
    static ToastManager& Instance();

    // Queue a notification. Returns immediately; rendering happens on the
    // overlay thread.
    void Show(ToastOptions options);
    // Dismiss every visible toast.
    void Clear();
    // Stop the overlay thread and destroy remaining windows. Called
    // automatically at process exit; safe to call explicitly.
    void Shutdown();

    // pimpl — public so the platform .cc can name the type.
    struct Impl;

private:
    ToastManager();
    ~ToastManager();
    ToastManager(const ToastManager&) = delete;
    ToastManager& operator=(const ToastManager&) = delete;

    std::unique_ptr<Impl> impl_;
};

// Convenience helpers mirroring the common notify.* call shapes.
inline void ToastShow(const std::string& title, const std::string& message,
                      ToastType type = ToastType::Info, int duration_ms = 4200) {
    ToastManager::Instance().Show({title, message, type, duration_ms, {}});
}
inline void ToastInfo(const std::string& t, const std::string& m,
                      int d = 4200) {
    ToastShow(t, m, ToastType::Info, d);
}
inline void ToastSuccess(const std::string& t, const std::string& m,
                         int d = 4200) {
    ToastShow(t, m, ToastType::Success, d);
}
inline void ToastWarning(const std::string& t, const std::string& m,
                         int d = 4200) {
    ToastShow(t, m, ToastType::Warning, d);
}
inline void ToastError(const std::string& t, const std::string& m,
                       int d = 6000) {
    ToastShow(t, m, ToastType::Error, d);
}

} // namespace XER::UI::Overlays

#endif // XER_UI_OVERLAYS_TOAST_H
