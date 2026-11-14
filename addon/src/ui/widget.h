#pragma once

// xer.XER.* / xer.UI.* — UI toolkit bindings (XER_UI_Core), matching
// addon/example.js:
//
//   class ContentWidget extends XER.Widget { ... }
//   const window = new XER.Window();
//   window.Create("Example", 800, 600);
//   window.SetContent(new ContentWidget());
//   const app = new XER.Application();
//   app.Initialize(); app.Run();

#include "util.h"
#include "callback.h"

#include <memory>

namespace XER::UI::Widgets {
class Widget;
}

namespace xer::addon::ui {

// JS wrapper holding the shared_ptr-owned C++ widget plus its callback
// store. Every widget JS object (Widget base, Label, Button, user subclasses
// via `extends`) unwraps to this type through its single internal field.
class WidgetWrap : public Nan::ObjectWrap {
public:
    explicit WidgetWrap(std::shared_ptr<XER::UI::Widgets::Widget> w,
                        std::shared_ptr<CallbackStore> callbacks = {});
    ~WidgetWrap() override;

    // XER.Widget ctor — creates a concrete Card container so
    // `class Foo extends XER.Widget` yields a usable widget.
    static void New(const Nan::FunctionCallbackInfo<v8::Value>& info);
    // XER.Label ctor — Label(text, muted).
    static void NewLabel(const Nan::FunctionCallbackInfo<v8::Value>& info);
    // Wrap an already-created widget into info.This() — used by every
    // concrete widget ctor binding. `callbacks` lets ctor-time callbacks
    // (menu item actions, on_complete) share the wrap's store.
    static void Adopt(const Nan::FunctionCallbackInfo<v8::Value>& info,
                      std::shared_ptr<XER::UI::Widgets::Widget> w,
                      std::shared_ptr<CallbackStore> callbacks = {});
    // Wrap an existing widget into a fresh JS object (Widget.prototype
    // instance) — for getters that return widgets (ScrollArea.GetContent).
    static v8::Local<v8::Object> ToJS(
        std::shared_ptr<XER::UI::Widgets::Widget> w);

    std::shared_ptr<XER::UI::Widgets::Widget> Get() const;
    std::shared_ptr<CallbackStore> Callbacks() const { return callbacks_; }

private:
    std::shared_ptr<XER::UI::Widgets::Widget> widget_;
    std::shared_ptr<CallbackStore> callbacks_;
};

WidgetWrap* UnwrapWidget(v8::Local<v8::Value> v);
std::shared_ptr<XER::UI::Widgets::Widget> WidgetFromValue(
    v8::Local<v8::Value> v);
std::shared_ptr<XER::UI::Widgets::Widget> WidgetFromArg(
    const Nan::FunctionCallbackInfo<v8::Value>& info, int index);

// The Widget FunctionTemplate — other widget classes Inherit() it so
// addChild/SetPosition/... resolve through the prototype chain.
v8::Local<v8::FunctionTemplate> WidgetBaseTemplate();

// Create an inheriting ctor template for a widget class.
inline v8::Local<v8::FunctionTemplate> MakeWidgetTemplate(
    Nan::FunctionCallback ctor, const char* js_name) {
    v8::Local<v8::FunctionTemplate> tpl =
        Nan::New<v8::FunctionTemplate>(ctor);
    tpl->InstanceTemplate()->SetInternalFieldCount(1);
    tpl->SetClassName(Nan::New(js_name).ToLocalChecked());
    tpl->Inherit(WidgetBaseTemplate());
    return tpl;
}

// Cast a widget JS object to its concrete C++ type (nullptr on mismatch).
template <typename T>
inline T* CastWidget(v8::Local<v8::Value> self) {
    WidgetWrap* wrap = UnwrapWidget(self);
    return wrap ? dynamic_cast<T*>(wrap->Get().get()) : nullptr;
}

// CallbackStore of a widget JS object (for SetOn* bindings).
inline std::shared_ptr<CallbackStore> CallbacksOf(v8::Local<v8::Value> self) {
    WidgetWrap* wrap = UnwrapWidget(self);
    return wrap ? wrap->Callbacks() : nullptr;
}

// Bind a JS function (arg_index) to a C++ std::function setter. Passing a
// non-function clears both sides. `set_cpp` receives the CallbackStore
// (shared_ptr) or nullptr.
template <typename SetFn>
inline void BindWidgetCallback(v8::Local<v8::Value> self,
                               const Nan::FunctionCallbackInfo<v8::Value>& info,
                               int arg_index, const char* key, SetFn&& set_cpp) {
    auto store = CallbacksOf(self);
    if (!store) return;
    if (xer::addon::HasArg(info, arg_index) &&
        info[arg_index]->IsFunction()) {
        store->Set(key, info[arg_index].As<v8::Function>());
        set_cpp(store);
    } else {
        store->Remove(key);
        set_cpp(nullptr);
    }
}

// Builds a std::function<R(Args...)> that emits `key` on `store`; returns an
// empty std::function when store is null (clears the C++ side). Use inside
// BindWidgetCallback setters:
//   w->SetOnClick(EmitFn<void>(store, "click"));
template <typename R, typename... Args>
inline std::function<R(Args...)> EmitFn(
    const std::shared_ptr<CallbackStore>& store, const char* key) {
    if (!store) return {};
    return [store, key](Args... a) { store->Emit(key, a...); };
}

// Register a concrete widget class: ctor template inheriting Widget +
// prototype methods.
inline void RegisterWidgetClass(
    v8::Local<v8::Object> xer, const char* js_name,
    Nan::FunctionCallback ctor,
    std::initializer_list<std::pair<const char*, Nan::FunctionCallback>>
        methods = {}) {
    v8::Local<v8::FunctionTemplate> tpl = MakeWidgetTemplate(ctor, js_name);
    for (const auto& [name, cb] : methods)
        Nan::SetMethod(tpl->PrototypeTemplate(), name, cb);
    Nan::Set(xer, Nan::New(js_name).ToLocalChecked(),
             Nan::GetFunction(tpl).ToLocalChecked());
}

// Per-area registration into the `xer.UI` namespace object.
void RegisterWidgets(v8::Local<v8::Object> xer);   // Widget + Label
void RegisterControls(v8::Local<v8::Object> xer);  // buttons/inputs/toggles
void RegisterDisplay(v8::Local<v8::Object> xer);   // text/badges/layout
void RegisterNav(v8::Local<v8::Object> xer);       // AppTitleBar
void RegisterKit(v8::Local<v8::Object> xer);       // kit primitives
void RegisterWindow(v8::Local<v8::Object> xer);
void RegisterApplication(v8::Local<v8::Object> xer);

}  // namespace xer::addon::ui
