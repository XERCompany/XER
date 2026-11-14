// xer.UI.* — display & overlay widgets: Avatar, Badge, Alert, Separator,
// Skeleton, Spinner, Progress, Accordion, Breadcrumb, ScrollArea, Tooltip,
// ContextMenu, Dialog, WaveformWidget, ScreenLoadingWidget.

#include "../widget.h"
#include "../../convert.h"

#include <memory>
#include <string>
#include <vector>

#include "widgets/avatar.h"
#include "widgets/badge.h"
#include "widgets/alert.h"
#include "widgets/separator.h"
#include "widgets/skeleton.h"
#include "widgets/spinner.h"
#include "widgets/progress.h"
#include "widgets/accordion.h"
#include "widgets/breadcrumb.h"
#include "widgets/scroll_area.h"
#include "widgets/tooltip.h"
#include "widgets/context_menu.h"
#include "widgets/dialog.h"
#include "widgets/waveform_widget.h"
#include "widgets/screen_loading_widget.h"
#include "widgets/card.h"

namespace {

namespace UIW = XER::UI::Widgets;
using xer::addon::HasArg;
using xer::addon::ArgUtf8;
using xer::addon::EnumArg;
using xer::addon::NumberOr;
using xer::addon::BoolOr;
using xer::addon::StringArray;
using xer::addon::RectFromJS;
using xer::addon::PointFromJS;
using xer::addon::CallbackStore;
using xer::addon::ui::WidgetWrap;
using xer::addon::ui::CastWidget;
using xer::addon::ui::CallbacksOf;
using xer::addon::ui::BindWidgetCallback;
using xer::addon::ui::EmitFn;
using xer::addon::ui::WidgetFromArg;
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

// --- Avatar -------------------------------------------------------------------

void NewAvatar(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Avatar>(
        StrArg(info, 0), static_cast<float>(NumArg(info, 1, 40.0))));
}

NAN_METHOD(AvatarSetName) {
    auto* w = CastWidget<UIW::Avatar>(info.This());
    std::string name;
    if (w && ArgUtf8(info, 0, &name)) w->SetName(name);
}

NAN_METHOD(AvatarGetName) {
    auto* w = CastWidget<UIW::Avatar>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetName()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(AvatarSetSize) {
    auto* w = CastWidget<UIW::Avatar>(info.This());
    if (w) w->SetSize(static_cast<float>(NumArg(info, 0, 40.0)));
}

// --- Badge --------------------------------------------------------------------

void NewBadge(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Badge>(
        StrArg(info, 0),
        EnumArg(info, 1, UIW::BadgeVariant::Default,
                {{"default", UIW::BadgeVariant::Default},
                 {"secondary", UIW::BadgeVariant::Secondary},
                 {"outline", UIW::BadgeVariant::Outline},
                 {"accent", UIW::BadgeVariant::Accent}})));
}

NAN_METHOD(BadgeSetText) {
    auto* w = CastWidget<UIW::Badge>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetText(text);
}

NAN_METHOD(BadgeGetText) {
    auto* w = CastWidget<UIW::Badge>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(BadgeSetVariant) {
    auto* w = CastWidget<UIW::Badge>(info.This());
    if (!w) return;
    w->SetVariant(EnumArg(info, 0, UIW::BadgeVariant::Default,
                          {{"default", UIW::BadgeVariant::Default},
                           {"secondary", UIW::BadgeVariant::Secondary},
                           {"outline", UIW::BadgeVariant::Outline},
                           {"accent", UIW::BadgeVariant::Accent}}));
}

// --- Alert --------------------------------------------------------------------

void NewAlert(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Alert>(
        StrArg(info, 0), StrArg(info, 1),
        EnumArg(info, 2, UIW::Alert::Variant::Default,
                {{"default", UIW::Alert::Variant::Default},
                 {"destructive", UIW::Alert::Variant::Destructive}})));
}

NAN_METHOD(AlertSetTitle) {
    auto* w = CastWidget<UIW::Alert>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetTitle(text);
}

NAN_METHOD(AlertSetDescription) {
    auto* w = CastWidget<UIW::Alert>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetDescription(text);
}

NAN_METHOD(AlertSetVariant) {
    auto* w = CastWidget<UIW::Alert>(info.This());
    if (!w) return;
    w->SetVariant(EnumArg(info, 0, UIW::Alert::Variant::Default,
                          {{"default", UIW::Alert::Variant::Default},
                           {"destructive", UIW::Alert::Variant::Destructive}}));
}

// --- Separator ----------------------------------------------------------------

void NewSeparator(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Separator>(
        EnumArg(info, 0, UIW::Orientation::Horizontal,
                {{"horizontal", UIW::Orientation::Horizontal},
                 {"vertical", UIW::Orientation::Vertical}})));
}

// --- Skeleton -----------------------------------------------------------------

void NewSkeleton(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Skeleton>(
        static_cast<float>(NumArg(info, 0, 0.0)),
        static_cast<float>(NumArg(info, 1, 0.0)),
        EnumArg(info, 2, UIW::Skeleton::Variant::Block,
                {{"block", UIW::Skeleton::Variant::Block},
                 {"text", UIW::Skeleton::Variant::Text}})));
}

NAN_METHOD(SkeletonSetSize) {
    auto* w = CastWidget<UIW::Skeleton>(info.This());
    if (w) w->SetSize(static_cast<float>(NumArg(info, 0, 0.0)),
                      static_cast<float>(NumArg(info, 1, 0.0)));
}

NAN_METHOD(SkeletonSetVariant) {
    auto* w = CastWidget<UIW::Skeleton>(info.This());
    if (!w) return;
    w->SetVariant(EnumArg(info, 0, UIW::Skeleton::Variant::Block,
                          {{"block", UIW::Skeleton::Variant::Block},
                           {"text", UIW::Skeleton::Variant::Text}}));
}

// --- Spinner / Progress -------------------------------------------------------

void NewSpinner(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(
        info, std::make_shared<UIW::Spinner>(static_cast<float>(NumArg(info, 0, 0.0))));
}

NAN_METHOD(SpinnerSetPhase) {
    auto* w = CastWidget<UIW::Spinner>(info.This());
    if (w) w->SetPhase(static_cast<float>(NumArg(info, 0, 0.0)));
}

void NewProgress(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(
        info, std::make_shared<UIW::Progress>(static_cast<float>(NumArg(info, 0, 0.0))));
}

NAN_METHOD(ProgressSetValue) {
    auto* w = CastWidget<UIW::Progress>(info.This());
    if (w) w->SetValue(static_cast<float>(NumArg(info, 0, 0.0)));
}

NAN_METHOD(ProgressGetValue) {
    auto* w = CastWidget<UIW::Progress>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? static_cast<double>(w->GetValue())
                                        : 0.0));
}

// --- Accordion ----------------------------------------------------------------

void NewAccordion(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Accordion>());
}

// JS: [{title, content: widget, open}]
NAN_METHOD(AccordionSetItems) {
    auto* w = CastWidget<UIW::Accordion>(info.This());
    if (!w || !HasArg(info, 0) || !info[0]->IsArray()) return;
    v8::Local<v8::Array> arr = info[0].As<v8::Array>();
    std::vector<UIW::Accordion::Item> items;
    const uint32_t n = arr->Length();
    items.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> el;
        arr->Get(Nan::GetCurrentContext(), i).ToLocal(&el);
        if (el.IsEmpty()) el = Nan::Undefined();
        if (!el->IsObject()) continue;
        v8::Local<v8::Object> o = el.As<v8::Object>();
        UIW::Accordion::Item item;
        item.title = xer::addon::StrProp(o, "title");
        item.content = xer::addon::ui::WidgetFromValue(xer::addon::GetProp(o, "content"));
        item.open = xer::addon::BoolProp(o, "open", false);
        items.push_back(std::move(item));
    }
    w->SetItems(std::move(items), BoolArg(info, 1, true));
}

NAN_METHOD(AccordionSetItemOpen) {
    auto* w = CastWidget<UIW::Accordion>(info.This());
    if (w) w->SetItemOpen(static_cast<std::size_t>(NumArg(info, 0, 0)),
                          BoolArg(info, 1, false));
}

NAN_METHOD(AccordionIsItemOpen) {
    auto* w = CastWidget<UIW::Accordion>(info.This());
    info.GetReturnValue().Set(Nan::New(
        w && w->IsItemOpen(static_cast<std::size_t>(NumArg(info, 0, 0)))));
}

NAN_METHOD(AccordionSetOnToggled) {
    auto* w = CastWidget<UIW::Accordion>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "toggled",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnToggled(
                               EmitFn<void, std::size_t, bool>(store, "toggled"));
                       });
}

// --- Breadcrumb ---------------------------------------------------------------

void NewBreadcrumb(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Breadcrumb>());
}

NAN_METHOD(BreadcrumbSetItems) {
    auto* w = CastWidget<UIW::Breadcrumb>(info.This());
    if (w && HasArg(info, 0)) w->SetItems(StringArray(info[0]));
}

NAN_METHOD(BreadcrumbSetOnNavigate) {
    auto* w = CastWidget<UIW::Breadcrumb>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "navigate",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnNavigate(EmitFn<void, int>(store, "navigate"));
                       });
}

// --- ScrollArea ---------------------------------------------------------------

void NewScrollArea(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto content = WidgetFromArg(info, 0);
    if (!content) content = std::make_shared<UIW::Card>();
    WidgetWrap::Adopt(info, std::make_shared<UIW::ScrollArea>(content));
}

NAN_METHOD(ScrollAreaSetContent) {
    auto* w = CastWidget<UIW::ScrollArea>(info.This());
    auto content = WidgetFromArg(info, 0);
    if (w && content) w->SetContent(content);
}

NAN_METHOD(ScrollAreaGetContent) {
    auto* w = CastWidget<UIW::ScrollArea>(info.This());
    if (!w || !w->GetContent()) {
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(WidgetWrap::ToJS(w->GetContent()));
}

NAN_METHOD(ScrollAreaScrollTo) {
    auto* w = CastWidget<UIW::ScrollArea>(info.This());
    if (w) w->ScrollTo(static_cast<float>(NumArg(info, 0, 0.0)));
}

NAN_METHOD(ScrollAreaGetScrollY) {
    auto* w = CastWidget<UIW::ScrollArea>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? static_cast<double>(w->GetScrollY())
                                        : 0.0));
}

NAN_METHOD(ScrollAreaGetScrollRange) {
    auto* w = CastWidget<UIW::ScrollArea>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w ? static_cast<double>(w->GetScrollRange()) : 0.0));
}

// --- Tooltip / ContextMenu / Dialog (overlay widgets) -------------------------

void NewTooltip(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, UIW::Tooltip::Create(StrArg(info, 0)));
}

NAN_METHOD(TooltipSetAnchor) {
    auto* w = CastWidget<UIW::Tooltip>(info.This());
    if (w && HasArg(info, 0)) w->SetAnchor(RectFromJS(info[0]));
}

// JS item: {label, action?, enabled?, checked?, shortcut?, separatorBefore?,
//           danger?}
UIW::ContextMenu::Item ContextMenuItemFromJS(
    v8::Local<v8::Object> o, std::shared_ptr<CallbackStore> store) {
    UIW::ContextMenu::Item item;
    item.label = xer::addon::StrProp(o, "label");
    item.enabled = xer::addon::BoolProp(o, "enabled", true);
    item.checked = xer::addon::BoolProp(o, "checked", false);
    item.danger = xer::addon::BoolProp(o, "danger", false);
    item.separator_before =
        xer::addon::BoolProp(o, "separatorBefore", xer::addon::BoolProp(o, "separator_before", false));
    item.shortcut = xer::addon::StrProp(o, "shortcut");
    v8::Local<v8::Value> action = xer::addon::GetProp(o, "action");
    if (action->IsFunction()) {
        const std::string key = store->Unique("item");
        store->Set(key.c_str(), action.As<v8::Function>());
        item.action = [store, key]() { store->Emit(key.c_str()); };
    }
    return item;
}

void NewContextMenu(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto store = std::make_shared<CallbackStore>();
    std::vector<UIW::ContextMenu::Item> items;
    if (HasArg(info, 0) && info[0]->IsArray()) {
        v8::Local<v8::Array> arr = info[0].As<v8::Array>();
        const uint32_t n = arr->Length();
        items.reserve(n);
        for (uint32_t i = 0; i < n; ++i) {
            v8::Local<v8::Value> el;
            arr->Get(Nan::GetCurrentContext(), i).ToLocal(&el);
            if (el.IsEmpty()) el = Nan::Undefined();
            if (el->IsObject())
                items.push_back(ContextMenuItemFromJS(el.As<v8::Object>(), store));
        }
    }
    WidgetWrap::Adopt(info, UIW::ContextMenu::Create(std::move(items)), store);
}

NAN_METHOD(ContextMenuSetAnchor) {
    auto* w = CastWidget<UIW::ContextMenu>(info.This());
    if (w && HasArg(info, 0)) w->SetAnchor(PointFromJS(info[0]));
}

NAN_METHOD(ContextMenuClose) {
    auto* w = CastWidget<UIW::ContextMenu>(info.This());
    if (w) w->Close();
}

void NewDialog(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto content = WidgetFromArg(info, 1);
    WidgetWrap::Adopt(info, UIW::Dialog::Create(
        StrArg(info, 0), content,
        HasArg(info, 2) ? StrArg(info, 2) : "OK",
        HasArg(info, 3) ? StrArg(info, 3) : ""));
}

NAN_METHOD(DialogSetTitle) {
    auto* w = CastWidget<UIW::Dialog>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetTitle(text);
}

NAN_METHOD(DialogSetContent) {
    auto* w = CastWidget<UIW::Dialog>(info.This());
    auto content = WidgetFromArg(info, 0);
    if (w && content) w->SetContent(content);
}

NAN_METHOD(DialogSetPanelWidth) {
    auto* w = CastWidget<UIW::Dialog>(info.This());
    if (w) w->SetPanelWidth(static_cast<float>(NumArg(info, 0, 0.0)));
}

NAN_METHOD(DialogSetOnClosed) {
    auto* w = CastWidget<UIW::Dialog>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "closed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnClosed(EmitFn<void>(store, "closed"));
                       });
}

NAN_METHOD(DialogClose) {
    auto* w = CastWidget<UIW::Dialog>(info.This());
    if (w) w->Close();
}

NAN_METHOD(DialogWasConfirmed) {
    auto* w = CastWidget<UIW::Dialog>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->WasConfirmed()));
}

// --- Waveform / ScreenLoading -------------------------------------------------

void NewWaveformWidget(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::WaveformWidget>());
}

NAN_METHOD(WaveformSetProgress) {
    auto* w = CastWidget<UIW::WaveformWidget>(info.This());
    if (w) w->SetProgress(static_cast<float>(NumArg(info, 0, 0.0)));
}

NAN_METHOD(WaveformSetPlaying) {
    auto* w = CastWidget<UIW::WaveformWidget>(info.This());
    if (w) w->SetPlaying(BoolArg(info, 0, false));
}

void NewScreenLoadingWidget(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto store = std::make_shared<CallbackStore>();
    UIW::ScreenLoadingWidget::CompleteCallback cb;
    if (HasArg(info, 0) && info[0]->IsFunction()) {
        store->Set("complete", info[0].As<v8::Function>());
        cb = [store]() { store->Emit("complete"); };
    }
    WidgetWrap::Adopt(info, std::make_shared<UIW::ScreenLoadingWidget>(cb),
                      store);
}

NAN_METHOD(ScreenLoadingStart) {
    auto* w = CastWidget<UIW::ScreenLoadingWidget>(info.This());
    if (w) w->Start();
}

NAN_METHOD(ScreenLoadingIsFinished) {
    auto* w = CastWidget<UIW::ScreenLoadingWidget>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsFinished()));
}

NAN_METHOD(ScreenLoadingTick) {
    auto* w = CastWidget<UIW::ScreenLoadingWidget>(info.This());
    if (w) w->Tick();
}

}  // namespace

namespace xer::addon::ui {

void RegisterDisplay(v8::Local<v8::Object> xer) {
    RegisterWidgetClass(xer, "Avatar", NewAvatar,
                        {{"SetName", AvatarSetName},
                         {"GetName", AvatarGetName},
                         {"SetSize", AvatarSetSize}});
    RegisterWidgetClass(xer, "Badge", NewBadge,
                        {{"SetText", BadgeSetText},
                         {"GetText", BadgeGetText},
                         {"SetVariant", BadgeSetVariant}});
    RegisterWidgetClass(xer, "Alert", NewAlert,
                        {{"SetTitle", AlertSetTitle},
                         {"SetDescription", AlertSetDescription},
                         {"SetVariant", AlertSetVariant}});
    RegisterWidgetClass(xer, "Separator", NewSeparator, {});
    RegisterWidgetClass(xer, "Skeleton", NewSkeleton,
                        {{"SetSize", SkeletonSetSize},
                         {"SetVariant", SkeletonSetVariant}});
    RegisterWidgetClass(xer, "Spinner", NewSpinner,
                        {{"SetPhase", SpinnerSetPhase}});
    RegisterWidgetClass(xer, "Progress", NewProgress,
                        {{"SetValue", ProgressSetValue},
                         {"GetValue", ProgressGetValue}});
    RegisterWidgetClass(xer, "Accordion", NewAccordion,
                        {{"SetItems", AccordionSetItems},
                         {"SetItemOpen", AccordionSetItemOpen},
                         {"IsItemOpen", AccordionIsItemOpen},
                         {"SetOnToggled", AccordionSetOnToggled}});
    RegisterWidgetClass(xer, "Breadcrumb", NewBreadcrumb,
                        {{"SetItems", BreadcrumbSetItems},
                         {"SetOnNavigate", BreadcrumbSetOnNavigate}});
    RegisterWidgetClass(xer, "ScrollArea", NewScrollArea,
                        {{"SetContent", ScrollAreaSetContent},
                         {"GetContent", ScrollAreaGetContent},
                         {"ScrollTo", ScrollAreaScrollTo},
                         {"GetScrollY", ScrollAreaGetScrollY},
                         {"GetScrollRange", ScrollAreaGetScrollRange}});
    RegisterWidgetClass(xer, "Tooltip", NewTooltip,
                        {{"SetAnchor", TooltipSetAnchor}});
    RegisterWidgetClass(xer, "ContextMenu", NewContextMenu,
                        {{"SetAnchor", ContextMenuSetAnchor},
                         {"Close", ContextMenuClose}});
    RegisterWidgetClass(xer, "Dialog", NewDialog,
                        {{"SetTitle", DialogSetTitle},
                         {"SetContent", DialogSetContent},
                         {"SetPanelWidth", DialogSetPanelWidth},
                         {"SetOnClosed", DialogSetOnClosed},
                         {"Close", DialogClose},
                         {"WasConfirmed", DialogWasConfirmed}});
    // Catalog alias: the modal confirm/cancel dialog covers alert-dialog.
    RegisterWidgetClass(xer, "AlertDialog", NewDialog,
                        {{"SetTitle", DialogSetTitle},
                         {"SetContent", DialogSetContent},
                         {"SetPanelWidth", DialogSetPanelWidth},
                         {"SetOnClosed", DialogSetOnClosed},
                         {"Close", DialogClose},
                         {"WasConfirmed", DialogWasConfirmed}});
    RegisterWidgetClass(xer, "WaveformWidget", NewWaveformWidget,
                        {{"SetProgress", WaveformSetProgress},
                         {"SetPlaying", WaveformSetPlaying}});
    RegisterWidgetClass(xer, "ScreenLoadingWidget", NewScreenLoadingWidget,
                        {{"Start", ScreenLoadingStart},
                         {"IsFinished", ScreenLoadingIsFinished},
                         {"Tick", ScreenLoadingTick}});
}

}  // namespace xer::addon::ui
