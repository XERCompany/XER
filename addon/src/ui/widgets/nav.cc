// xer.UI.AppTitleBar — IDE-style titlebar (workspace switcher, menus,
// command pill, panel toggles, theme switch, avatar, action pill, window
// commands). Mirrors preview.exe's titlebar usage.

#include "../widget.h"
#include "../../convert.h"

#include <memory>
#include <string>
#include <vector>

#include "widgets/app_title_bar.h"

namespace {

namespace UIW = XER::UI::Widgets;
using xer::addon::HasArg;
using xer::addon::ArgUtf8;
using xer::addon::NumberOr;
using xer::addon::BoolOr;
using xer::addon::PointFromJS;
using xer::addon::CallbackStore;
using xer::addon::ui::WidgetWrap;
using xer::addon::ui::CastWidget;
using xer::addon::ui::CallbacksOf;
using xer::addon::ui::BindWidgetCallback;
using xer::addon::ui::RegisterWidgetClass;

double NumArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
              double fallback) {
    return HasArg(info, index) ? NumberOr(info[index], fallback) : fallback;
}

bool BoolArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index,
             bool fallback) {
    return HasArg(info, index) ? BoolOr(info[index], fallback) : fallback;
}

std::string StrArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                   int index) {
    std::string s;
    ArgUtf8(info, index, &s);
    return s;
}

// --- MenuItem / Menu converters ------------------------------------------------
// JS MenuItem: {label, shortcut?, action?, enabled?, checked?, danger?,
//               separatorBefore?}
// JS Menu:      {label, mnemonic?, items:[MenuItem...]}

UIW::AppTitleBar::MenuItem MenuItemFromJS(
    v8::Local<v8::Object> o, std::shared_ptr<CallbackStore> store) {
    UIW::AppTitleBar::MenuItem item;
    item.label = xer::addon::StrProp(o, "label");
    item.shortcut = xer::addon::StrProp(o, "shortcut");
    item.enabled = xer::addon::BoolProp(o, "enabled", true);
    item.checked = xer::addon::BoolProp(o, "checked", false);
    item.danger = xer::addon::BoolProp(o, "danger", false);
    item.separator_before = xer::addon::BoolProp(
        o, "separatorBefore",
        xer::addon::BoolProp(o, "separator_before", false));
    v8::Local<v8::Value> action = xer::addon::GetProp(o, "action");
    if (action->IsFunction()) {
        const std::string key = store->Unique("mi");
        store->Set(key.c_str(), action.As<v8::Function>());
        item.action = [store, key]() { store->Emit(key.c_str()); };
    }
    return item;
}

std::vector<UIW::AppTitleBar::MenuItem> MenuItemsFromJS(
    v8::Local<v8::Value> v, std::shared_ptr<CallbackStore> store) {
    std::vector<UIW::AppTitleBar::MenuItem> out;
    if (v.IsEmpty() || !v->IsArray()) return out;
    v8::Local<v8::Array> arr = v.As<v8::Array>();
    const uint32_t n = arr->Length();
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> el =
            arr->Get(Nan::GetCurrentContext(), i).FromMaybe(Nan::Undefined());
        if (el->IsObject())
            out.push_back(MenuItemFromJS(el.As<v8::Object>(), store));
    }
    return out;
}

UIW::AppTitleBar::Menu MenuFromJS(v8::Local<v8::Object> o,
                                  std::shared_ptr<CallbackStore> store) {
    UIW::AppTitleBar::Menu menu;
    menu.label = xer::addon::StrProp(o, "label");
    const std::string mnem = xer::addon::StrProp(o, "mnemonic");
    menu.mnemonic = mnem.empty() ? '\0' : mnem[0];
    menu.items = MenuItemsFromJS(xer::addon::GetProp(o, "items"), store);
    return menu;
}

// --- widget -------------------------------------------------------------------

void NewAppTitleBar(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info,
                      std::make_shared<UIW::AppTitleBar>(StrArg(info, 0)));
}

NAN_METHOD(TitleBarSetWorkspaceLabel) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetWorkspaceLabel(text);
}

NAN_METHOD(TitleBarGetWorkspaceLabel) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetWorkspaceLabel()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(TitleBarSetWorkspaceItems) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    auto store = CallbacksOf(info.This());
    if (!w || !store || !HasArg(info, 0)) return;
    w->SetWorkspaceItems(MenuItemsFromJS(info[0], store));
}

NAN_METHOD(TitleBarSetNavActions) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    const bool back = BoolArg(info, 0, false);
    const bool fwd = BoolArg(info, 1, false);
    auto store = CallbacksOf(info.This());
    auto back_fn = [store]() { if (store) store->Emit("navBack"); };
    auto fwd_fn = [store]() { if (store) store->Emit("navForward"); };
    if (store && HasArg(info, 2) && info[2]->IsFunction())
        store->Set("navBack", info[2].As<v8::Function>());
    else if (store)
        store->Remove("navBack");
    if (store && HasArg(info, 3) && info[3]->IsFunction())
        store->Set("navForward", info[3].As<v8::Function>());
    else if (store)
        store->Remove("navForward");
    w->SetNavActions(back, fwd,
                     store ? std::function<void()>(back_fn) : nullptr,
                     store ? std::function<void()>(fwd_fn) : nullptr);
}

NAN_METHOD(TitleBarSetMenus) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    auto store = CallbacksOf(info.This());
    if (!w || !store || !HasArg(info, 0) || !info[0]->IsArray()) return;
    v8::Local<v8::Array> arr = info[0].As<v8::Array>();
    const uint32_t n = arr->Length();
    std::vector<UIW::AppTitleBar::Menu> menus;
    menus.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> el =
            arr->Get(Nan::GetCurrentContext(), i).FromMaybe(Nan::Undefined());
        if (el->IsObject())
            menus.push_back(MenuFromJS(el.As<v8::Object>(), store));
    }
    w->SetMenus(std::move(menus));
}

NAN_METHOD(TitleBarClearMenus) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (w) w->ClearMenus();
}

NAN_METHOD(TitleBarSetCenterCommand) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    auto store = CallbacksOf(info.This());
    if (store && HasArg(info, 1) && info[1]->IsFunction()) {
        store->Set("centerActivate", info[1].As<v8::Function>());
        w->SetCenterCommand(StrArg(info, 0),
                            [store]() { store->Emit("centerActivate"); },
                            BoolArg(info, 2, true));
    } else {
        if (store) store->Remove("centerActivate");
        w->SetCenterCommand(StrArg(info, 0), nullptr, BoolArg(info, 2, true));
    }
}

NAN_METHOD(TitleBarSetCenterCommandValue) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetCenterCommandValue(text);
}

NAN_METHOD(TitleBarGetCenterCommandValue) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetCenterCommandValue()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(TitleBarSetLeftPanelToggle) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    auto store = CallbacksOf(info.This());
    if (store && HasArg(info, 2) && info[2]->IsFunction())
        store->Set("leftPanel", info[2].As<v8::Function>());
    else if (store)
        store->Remove("leftPanel");
    w->SetLeftPanelToggle(
        BoolArg(info, 0, false), BoolArg(info, 1, false),
        store ? std::function<void()>([store]() {
                   if (store) store->Emit("leftPanel");
               })
              : nullptr);
}

NAN_METHOD(TitleBarSetRightPanelToggle) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    auto store = CallbacksOf(info.This());
    if (store && HasArg(info, 2) && info[2]->IsFunction())
        store->Set("rightPanel", info[2].As<v8::Function>());
    else if (store)
        store->Remove("rightPanel");
    w->SetRightPanelToggle(
        BoolArg(info, 0, false), BoolArg(info, 1, false),
        store ? std::function<void()>([store]() {
                   if (store) store->Emit("rightPanel");
               })
              : nullptr);
}

NAN_METHOD(TitleBarSetThemeToggle) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    auto store = CallbacksOf(info.This());
    if (store && HasArg(info, 1) && info[1]->IsFunction())
        store->Set("themeChanged", info[1].As<v8::Function>());
    else if (store)
        store->Remove("themeChanged");
    w->SetThemeToggle(
        BoolArg(info, 0, false),
        store ? std::function<void(bool)>([store](bool dark) {
                   store->Emit("themeChanged", dark);
               })
              : nullptr);
}

NAN_METHOD(TitleBarSetAvatar) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    auto store = CallbacksOf(info.This());
    if (!w || !store) return;
    w->SetAvatar(StrArg(info, 0),
                 HasArg(info, 1) ? MenuItemsFromJS(info[1], store)
                                 : std::vector<UIW::AppTitleBar::MenuItem>{});
}

NAN_METHOD(TitleBarSetActionPill) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    auto store = CallbacksOf(info.This());
    if (store && HasArg(info, 1) && info[1]->IsFunction())
        store->Set("actionPill", info[1].As<v8::Function>());
    else if (store)
        store->Remove("actionPill");
    w->SetActionPill(
        StrArg(info, 0),
        store ? std::function<void()>([store]() {
                   if (store) store->Emit("actionPill");
               })
              : nullptr,
        BoolArg(info, 2, false));
}

NAN_METHOD(TitleBarClearActionPill) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (w) w->ClearActionPill();
}

NAN_METHOD(TitleBarSetOnWindowCommand) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "windowCommand",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnWindowCommand(
                               store ? [store](UIW::AppTitleBar::WindowCommand cmd) {
                                   store->Emit("windowCommand",
                                               static_cast<int>(cmd));
                               } : nullptr);
                       });
}

NAN_METHOD(TitleBarSetMaximized) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    if (w) w->SetMaximized(BoolArg(info, 0, false));
}

NAN_METHOD(TitleBarGetMaximized) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->GetMaximized()));
}

NAN_METHOD(TitleBarIsInteractiveAt) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->IsInteractiveAt(PointFromJS(
            HasArg(info, 0) ? info[0] : v8::Local<v8::Value>()))));
}

NAN_METHOD(TitleBarHasInteractiveAt) {
    auto* w = CastWidget<UIW::AppTitleBar>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->HasInteractiveAt(PointFromJS(
            HasArg(info, 0) ? info[0] : v8::Local<v8::Value>()))));
}

}  // namespace

namespace xer::addon::ui {

void RegisterNav(v8::Local<v8::Object> xer) {
    RegisterWidgetClass(xer, "AppTitleBar", NewAppTitleBar,
                        {{"SetWorkspaceLabel", TitleBarSetWorkspaceLabel},
                         {"GetWorkspaceLabel", TitleBarGetWorkspaceLabel},
                         {"SetWorkspaceItems", TitleBarSetWorkspaceItems},
                         {"SetNavActions", TitleBarSetNavActions},
                         {"SetMenus", TitleBarSetMenus},
                         {"ClearMenus", TitleBarClearMenus},
                         {"SetCenterCommand", TitleBarSetCenterCommand},
                         {"SetCenterCommandValue", TitleBarSetCenterCommandValue},
                         {"GetCenterCommandValue", TitleBarGetCenterCommandValue},
                         {"SetLeftPanelToggle", TitleBarSetLeftPanelToggle},
                         {"SetRightPanelToggle", TitleBarSetRightPanelToggle},
                         {"SetThemeToggle", TitleBarSetThemeToggle},
                         {"SetAvatar", TitleBarSetAvatar},
                         {"SetActionPill", TitleBarSetActionPill},
                         {"ClearActionPill", TitleBarClearActionPill},
                         {"SetOnWindowCommand", TitleBarSetOnWindowCommand},
                         {"SetMaximized", TitleBarSetMaximized},
                         {"GetMaximized", TitleBarGetMaximized},
                         {"IsInteractiveAt", TitleBarIsInteractiveAt},
                         {"HasInteractiveAt", TitleBarHasInteractiveAt}});
}

}  // namespace xer::addon::ui
