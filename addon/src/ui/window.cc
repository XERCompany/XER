// xer.UI.Window — top-level window (XER_UI_Core::Window).

#include "widget.h"
#include "convert.h"

#include <memory>
#include <string>

#include "core/window.h"
#include "theme/theme.h"
#include "layout/geometry.h"

namespace {

namespace UICore = XER::UI::Core;
namespace UIL = XER::UI::Layout;
using xer::addon::CallbackStore;

class WindowWrap : public Nan::ObjectWrap {
public:
    WindowWrap() : window_(std::make_shared<UICore::Window>()),
                   callbacks_(std::make_shared<CallbackStore>()) {}
    ~WindowWrap() override {
        // Same teardown hazard as WidgetWrap: ~Window / Nan::Persistent::Reset
        // are unsafe once the isolate is disposing. Leak both.
        static auto* leaked_windows =
            new std::vector<std::shared_ptr<UICore::Window>>();
        static auto* leaked_stores =
            new std::vector<std::shared_ptr<CallbackStore>>();
        leaked_windows->push_back(std::move(window_));
        leaked_stores->push_back(std::move(callbacks_));
    }

    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    std::shared_ptr<UICore::Window> Get() const { return window_; }
    std::shared_ptr<CallbackStore> Callbacks() const { return callbacks_; }

private:
    std::shared_ptr<UICore::Window> window_;
    std::shared_ptr<CallbackStore> callbacks_;
};

void WindowWrap::New(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* wrap = new WindowWrap();
    wrap->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
}

WindowWrap* UnwrapWindow(v8::Local<v8::Value> v) {
    if (v.IsEmpty() || !v->IsObject()) return nullptr;
    return Nan::ObjectWrap::Unwrap<WindowWrap>(v.As<v8::Object>());
}

// --- argument helpers ---------------------------------------------------------

int IntArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
           int fallback) {
    return xer::addon::HasArg(info, index) && info[index]->IsNumber()
               ? static_cast<int>(info[index].As<v8::Number>()->Value())
               : fallback;
}

float FloatArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
               float fallback) {
    return xer::addon::HasArg(info, index) && info[index]->IsNumber()
               ? static_cast<float>(info[index].As<v8::Number>()->Value())
               : fallback;
}

bool BoolArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
             bool fallback) {
    return xer::addon::HasArg(info, index) && info[index]->IsBoolean()
               ? info[index].As<v8::Boolean>()->Value()
               : fallback;
}

// --- lifecycle ----------------------------------------------------------------

NAN_METHOD(WindowCreate) {
    auto* self = UnwrapWindow(info.This());
    if (!self) {
        Nan::ThrowError("window.Create on non-window object");
        return;
    }
    std::string title;
    xer::addon::ArgUtf8(info, 0, &title);
    info.GetReturnValue().Set(Nan::New(self->Get()->Create(
        title, IntArg(info, 1, 800), IntArg(info, 2, 600))));
}

NAN_METHOD(WindowShow) {
    auto* self = UnwrapWindow(info.This());
    if (self) self->Get()->Show();
}

NAN_METHOD(WindowHide) {
    auto* self = UnwrapWindow(info.This());
    if (self) self->Get()->Hide();
}

NAN_METHOD(WindowClose) {
    auto* self = UnwrapWindow(info.This());
    if (self) self->Get()->Close();
}

NAN_METHOD(WindowIsClosed) {
    auto* self = UnwrapWindow(info.This());
    info.GetReturnValue().Set(Nan::New(self && self->Get()->IsClosed()));
}

NAN_METHOD(WindowIsVisible) {
    auto* self = UnwrapWindow(info.This());
    info.GetReturnValue().Set(Nan::New(self && self->Get()->IsVisible()));
}

// --- geometry -----------------------------------------------------------------

NAN_METHOD(WindowSetSize) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    self->Get()->SetSize(IntArg(info, 0, 0), IntArg(info, 1, 0));
}

NAN_METHOD(WindowGetSize) {
    auto* self = UnwrapWindow(info.This());
    if (!self) {
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    int w = 0, h = 0;
    self->Get()->GetSize(&w, &h);
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("width").ToLocalChecked(), Nan::New(w));
    Nan::Set(obj, Nan::New("height").ToLocalChecked(), Nan::New(h));
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(WindowSetPosition) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    self->Get()->SetPosition(IntArg(info, 0, 0), IntArg(info, 1, 0));
}

NAN_METHOD(WindowGetPosition) {
    auto* self = UnwrapWindow(info.This());
    if (!self) {
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    int x = 0, y = 0;
    self->Get()->GetPosition(&x, &y);
    v8::Local<v8::Object> obj = Nan::New<v8::Object>();
    Nan::Set(obj, Nan::New("x").ToLocalChecked(), Nan::New(x));
    Nan::Set(obj, Nan::New("y").ToLocalChecked(), Nan::New(y));
    info.GetReturnValue().Set(obj);
}

NAN_METHOD(WindowSetSizeLimits) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    self->Get()->SetSizeLimits(IntArg(info, 0, 0), IntArg(info, 1, 0),
                               IntArg(info, 2, 0), IntArg(info, 3, 0));
}

NAN_METHOD(WindowMinimize) {
    auto* self = UnwrapWindow(info.This());
    if (self) self->Get()->Minimize();
}

NAN_METHOD(WindowToggleMaximize) {
    auto* self = UnwrapWindow(info.This());
    if (self) self->Get()->ToggleMaximize();
}

NAN_METHOD(WindowIsMaximized) {
    auto* self = UnwrapWindow(info.This());
    info.GetReturnValue().Set(
        Nan::New(self && self->Get()->IsWindowMaximized()));
}

// --- content / chrome ---------------------------------------------------------

NAN_METHOD(WindowSetContent) {
    auto* self = UnwrapWindow(info.This());
    auto widget = xer::addon::ui::WidgetFromArg(info, 0);
    if (!self || !widget) {
        Nan::ThrowError("window.SetContent(widget) expects a XER widget");
        return;
    }
    self->Get()->SetContent(widget);
}

NAN_METHOD(WindowGetContent) {
    auto* self = UnwrapWindow(info.This());
    if (!self || !self->Get()->GetContent()) {
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(xer::addon::ui::WidgetWrap::ToJS(
        self->Get()->GetContent()));
}

NAN_METHOD(WindowSetTitle) {
    auto* self = UnwrapWindow(info.This());
    std::string title;
    if (!self || !xer::addon::ArgUtf8(info, 0, &title)) return;
    self->Get()->SetTitle(title);
}

NAN_METHOD(WindowGetTitle) {
    auto* self = UnwrapWindow(info.This());
    if (!self) {
        info.GetReturnValue().Set(Nan::New("").ToLocalChecked());
        return;
    }
    info.GetReturnValue().Set(
        Nan::New(self->Get()->GetTitle()).ToLocalChecked());
}

NAN_METHOD(WindowSetTheme) {
    auto* self = UnwrapWindow(info.This());
    std::string theme;
    if (!self || !xer::addon::ArgUtf8(info, 0, &theme)) return;
    self->Get()->SetTheme(
        theme == "light" ? XER::UI::Theme::Variant::Light
                         : XER::UI::Theme::Variant::Dark);
}

NAN_METHOD(WindowGetTheme) {
    auto* self = UnwrapWindow(info.This());
    const bool dark = self &&
        self->Get()->GetTheme() == XER::UI::Theme::Variant::Dark;
    info.GetReturnValue().Set(
        Nan::New(dark ? "dark" : "light").ToLocalChecked());
}

NAN_METHOD(WindowSetCustomTitleBar) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    self->Get()->SetCustomTitleBar(BoolArg(info, 0, true),
                                   FloatArg(info, 1, 38.0f));
}

NAN_METHOD(WindowHasCustomTitleBar) {
    auto* self = UnwrapWindow(info.This());
    info.GetReturnValue().Set(
        Nan::New(self && self->Get()->HasCustomTitleBar()));
}

NAN_METHOD(WindowSetCloseHides) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    self->Get()->SetCloseHides(BoolArg(info, 0, true));
}

NAN_METHOD(WindowInvalidate) {
    auto* self = UnwrapWindow(info.This());
    if (self) self->Get()->Invalidate();
}

NAN_METHOD(WindowGetLastRenderError) {
    auto* self = UnwrapWindow(info.This());
    if (!self) {
        info.GetReturnValue().Set(Nan::New("").ToLocalChecked());
        return;
    }
    info.GetReturnValue().Set(
        Nan::New(self->Get()->GetLastRenderError()).ToLocalChecked());
}

NAN_METHOD(WindowGetHWND) {
    // Opaque native handle (Win32 HWND as a number; 0 elsewhere).
    auto* self = UnwrapWindow(info.This());
    const uintptr_t hwnd = self ? reinterpret_cast<uintptr_t>(self->Get()->GetHWND()) : 0;
    info.GetReturnValue().Set(Nan::New(static_cast<double>(hwnd)));
}

// --- callbacks ----------------------------------------------------------------

// Stores a JS function under `key` (or clears the C++ callback when the arg
// is not a function).
template <typename SetFn>
void BindCallback(WindowWrap* self, const Nan::FunctionCallbackInfo<v8::Value>& info,
                  int arg_index, const char* key, SetFn&& set_cpp) {
    if (!self) return;
    auto store = self->Callbacks();
    if (xer::addon::HasArg(info, arg_index) &&
        info[arg_index]->IsFunction()) {
        store->Set(key, info[arg_index].As<v8::Function>());
        set_cpp(store);
    } else {
        store->Remove(key);
        set_cpp(nullptr);
    }
}

NAN_METHOD(WindowSetOnClose) {
    auto* self = UnwrapWindow(info.This());
    BindCallback(self, info, 0, "close",
                 [&](std::shared_ptr<CallbackStore> s) {
                     if (s) {
                         self->Get()->SetOnClose([s]() { s->Emit("close"); });
                     } else {
                         self->Get()->SetOnClose(nullptr);
                     }
                 });
}

NAN_METHOD(WindowSetOnResize) {
    auto* self = UnwrapWindow(info.This());
    BindCallback(self, info, 0, "resize",
                 [&](std::shared_ptr<CallbackStore> s) {
                     if (s) {
                         self->Get()->SetOnResize(
                             [s](int w, int h) { s->Emit("resize", w, h); });
                     } else {
                         self->Get()->SetOnResize(nullptr);
                     }
                 });
}

NAN_METHOD(WindowSetFrameCallback) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    auto store = self->Callbacks();
    if (xer::addon::HasArg(info, 0) && info[0]->IsFunction()) {
        store->Set("frame", info[0].As<v8::Function>());
        const std::uint32_t interval =
            static_cast<std::uint32_t>(IntArg(info, 1, 33));
        self->Get()->SetFrameCallback(
            [store]() { store->Emit("frame"); }, interval);
    } else {
        store->Remove("frame");
        self->Get()->SetFrameCallback(nullptr, 33);
    }
}

NAN_METHOD(WindowSetTitleBarHitTest) {
    auto* self = UnwrapWindow(info.This());
    if (!self) return;
    auto store = self->Callbacks();
    if (xer::addon::HasArg(info, 0) && info[0]->IsFunction()) {
        store->Set("hitTest", info[0].As<v8::Function>());
        self->Get()->SetTitleBarHitTest([store](UIL::Point p) {
            v8::Local<v8::Value> r = store->Call("hitTest", p);
            return r->IsBoolean() && r.As<v8::Boolean>()->Value();
        });
    } else {
        store->Remove("hitTest");
        self->Get()->SetTitleBarHitTest(nullptr);
    }
}

}  // namespace

namespace xer::addon::ui {

void RegisterWindow(v8::Local<v8::Object> xer) {
    v8::Local<v8::FunctionTemplate> win_tpl =
        Nan::New<v8::FunctionTemplate>(WindowWrap::New);
    win_tpl->InstanceTemplate()->SetInternalFieldCount(1);
    win_tpl->SetClassName(Nan::New("Window").ToLocalChecked());

    auto proto = win_tpl->PrototypeTemplate();
    Nan::SetMethod(proto, "Create", WindowCreate);
    Nan::SetMethod(proto, "Show", WindowShow);
    Nan::SetMethod(proto, "Hide", WindowHide);
    Nan::SetMethod(proto, "Close", WindowClose);
    Nan::SetMethod(proto, "IsClosed", WindowIsClosed);
    Nan::SetMethod(proto, "IsVisible", WindowIsVisible);
    Nan::SetMethod(proto, "SetSize", WindowSetSize);
    Nan::SetMethod(proto, "GetSize", WindowGetSize);
    Nan::SetMethod(proto, "SetPosition", WindowSetPosition);
    Nan::SetMethod(proto, "GetPosition", WindowGetPosition);
    Nan::SetMethod(proto, "SetSizeLimits", WindowSetSizeLimits);
    Nan::SetMethod(proto, "Minimize", WindowMinimize);
    Nan::SetMethod(proto, "ToggleMaximize", WindowToggleMaximize);
    Nan::SetMethod(proto, "IsWindowMaximized", WindowIsMaximized);
    Nan::SetMethod(proto, "SetContent", WindowSetContent);
    Nan::SetMethod(proto, "GetContent", WindowGetContent);
    Nan::SetMethod(proto, "SetTitle", WindowSetTitle);
    Nan::SetMethod(proto, "GetTitle", WindowGetTitle);
    Nan::SetMethod(proto, "SetTheme", WindowSetTheme);
    Nan::SetMethod(proto, "GetTheme", WindowGetTheme);
    Nan::SetMethod(proto, "SetCustomTitleBar", WindowSetCustomTitleBar);
    Nan::SetMethod(proto, "HasCustomTitleBar", WindowHasCustomTitleBar);
    Nan::SetMethod(proto, "SetCloseHides", WindowSetCloseHides);
    Nan::SetMethod(proto, "Invalidate", WindowInvalidate);
    Nan::SetMethod(proto, "GetLastRenderError", WindowGetLastRenderError);
    Nan::SetMethod(proto, "GetHWND", WindowGetHWND);
    Nan::SetMethod(proto, "SetOnClose", WindowSetOnClose);
    Nan::SetMethod(proto, "SetOnResize", WindowSetOnResize);
    Nan::SetMethod(proto, "SetFrameCallback", WindowSetFrameCallback);
    Nan::SetMethod(proto, "SetTitleBarHitTest", WindowSetTitleBarHitTest);

    Nan::Set(xer, Nan::New("Window").ToLocalChecked(),
             Nan::GetFunction(win_tpl).ToLocalChecked());
}

}  // namespace xer::addon::ui
