// xer.UI.* — kit primitives: Kbd, Marker, AspectRatio, ButtonGroup,
// InputGroup, Field, Item, Empty, Collapsible.

#include "../widget.h"
#include "../../convert.h"

#include <memory>
#include <string>
#include <utility>

#include "widgets/kbd.h"
#include "widgets/marker.h"
#include "widgets/aspect_ratio.h"
#include "widgets/button_group.h"
#include "widgets/input_group.h"
#include "widgets/field.h"
#include "widgets/item.h"
#include "widgets/empty.h"
#include "widgets/collapsible.h"
#include "widgets/direction.h"
#include "widgets/form.h"
#include "widgets/attachment.h"
#include "widgets/bubble.h"
#include "widgets/message.h"
#include "widgets/message_scroller.h"
#include "widgets/input_otp.h"
#include "widgets/hover_card.h"
#include "widgets/popover.h"
#include "widgets/dropdown_menu.h"
#include "widgets/sheet.h"
#include "widgets/drawer.h"
#include "widgets/resizable.h"
#include "widgets/carousel.h"

namespace {

namespace UIW = XER::UI::Widgets;
using xer::addon::HasArg;
using xer::addon::ArgUtf8;
using xer::addon::NumberOr;
using xer::addon::BoolOr;
using xer::addon::EnumArg;
using xer::addon::ui::WidgetWrap;
using xer::addon::ui::WidgetFromArg;
using xer::addon::ui::CastWidget;
using xer::addon::ui::BindWidgetCallback;
using xer::addon::ui::EmitFn;
using xer::addon::ui::RegisterWidgetClass;
using xer::addon::CallbackStore;

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

// String get/set pairs — one macro keeps the nine widgets compact.
#define XER_CAT2(a, b) a##b
#define XER_CAT(a, b) XER_CAT2(a, b)
#define KIT_TEXT(CLASS, NAME)                                             \
    NAN_METHOD(XER_CAT(CLASS, XER_CAT(Get, NAME))) {                      \
        auto* w = CastWidget<UIW::CLASS>(info.This());                    \
        info.GetReturnValue().Set(                                        \
            w ? Nan::New(w->XER_CAT(Get, NAME)()).ToLocalChecked()         \
              : Nan::New("").ToLocalChecked());                            \
    }                                                                     \
    NAN_METHOD(XER_CAT(CLASS, XER_CAT(Set, NAME))) {                      \
        auto* w = CastWidget<UIW::CLASS>(info.This());                    \
        std::string v;                                                    \
        if (w && ArgUtf8(info, 0, &v))                                    \
            w->XER_CAT(Set, NAME)(std::move(v));                          \
    }

KIT_TEXT(Kbd, Text)
KIT_TEXT(Marker, Text)

void NewKbd(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Kbd>(StrArg(info, 0)));
}

void NewMarker(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Marker>(StrArg(info, 0)));
}

KIT_TEXT(Field, Label)
KIT_TEXT(Field, Description)
KIT_TEXT(Field, Error)
KIT_TEXT(InputGroup, Prefix)
KIT_TEXT(InputGroup, Suffix)
KIT_TEXT(Item, Title)
KIT_TEXT(Item, Description)
KIT_TEXT(Empty, Title)
KIT_TEXT(Empty, Description)

#undef KIT_TEXT
#undef XER_CAT
#undef XER_CAT2

template <typename W, typename... Args>
void NewSimple(const Nan::FunctionCallbackInfo<v8::Value>& info,
               Args&&... args) {
    WidgetWrap::Adopt(info, std::make_shared<W>(std::forward<Args>(args)...));
}

// --- AspectRatio -------------------------------------------------------------

void NewAspectRatio(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::AspectRatio>(
        info, static_cast<float>(NumArg(info, 0, 16.0 / 9.0)));
}

NAN_METHOD(AspectRatioSetRatio) {
    auto* w = CastWidget<UIW::AspectRatio>(info.This());
    if (w) w->SetRatio(static_cast<float>(NumArg(info, 0, w->GetRatio())));
}

NAN_METHOD(AspectRatioGetRatio) {
    auto* w = CastWidget<UIW::AspectRatio>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetRatio() : 0.0));
}

// --- ButtonGroup -------------------------------------------------------------

using BtnOrientation = UIW::ButtonGroup::Orientation;

BtnOrientation OrientationArg(
    const Nan::FunctionCallbackInfo<v8::Value>& info, int index) {
    return EnumArg(info, index, BtnOrientation::Horizontal,
                   {{"Horizontal", BtnOrientation::Horizontal},
                    {"Vertical", BtnOrientation::Vertical}});
}

void NewButtonGroup(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::ButtonGroup>(info, OrientationArg(info, 0));
}

NAN_METHOD(ButtonGroupSetOrientation) {
    auto* w = CastWidget<UIW::ButtonGroup>(info.This());
    if (w) w->SetOrientation(OrientationArg(info, 0));
}

NAN_METHOD(ButtonGroupGetOrientation) {
    auto* w = CastWidget<UIW::ButtonGroup>(info.This());
    const char* name = "horizontal";
    if (w && w->GetOrientation() == BtnOrientation::Vertical)
        name = "vertical";
    info.GetReturnValue().Set(Nan::New(name).ToLocalChecked());
}

// --- Collapsible ------------------------------------------------------------

void NewCollapsible(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::Collapsible>(info, BoolArg(info, 0, false));
}

NAN_METHOD(CollapsibleSetOpen) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    if (w) w->SetOpen(BoolArg(info, 0, w->IsOpen()));
}

NAN_METHOD(CollapsibleIsOpen) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsOpen()));
}

NAN_METHOD(CollapsibleGetProgress) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetProgress() : 0.0));
}

NAN_METHOD(CollapsibleSetOnToggled) {
    auto* w = CastWidget<UIW::Collapsible>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "toggled",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnToggled(
                               EmitFn<void, bool>(store, "toggled"));
                       });
}

// --- Item selected ------------------------------------------------------------

NAN_METHOD(ItemSetSelected) {
    auto* w = CastWidget<UIW::Item>(info.This());
    if (w) w->SetSelected(BoolArg(info, 0, w->IsSelected()));
}

NAN_METHOD(ItemIsSelected) {
    auto* w = CastWidget<UIW::Item>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsSelected()));
}

// --- Direction ----------------------------------------------------------------

using TextDir = XER::UI::Widgets::TextDirection;

TextDir DirArg(const Nan::FunctionCallbackInfo<v8::Value>& info, int index) {
    return EnumArg(info, index, TextDir::Ltr,
                   {{"Inherit", TextDir::Inherit},
                    {"Ltr", TextDir::Ltr},
                    {"Rtl", TextDir::Rtl}});
}

void NewDirection(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::Direction>(info, DirArg(info, 0));
}

NAN_METHOD(DirectionSetDirection) {
    auto* w = CastWidget<UIW::Direction>(info.This());
    if (w) w->SetDirection(DirArg(info, 0));
}

NAN_METHOD(DirectionGetDirection) {
    auto* w = CastWidget<UIW::Direction>(info.This());
    const char* name = "ltr";
    if (w) {
        switch (w->GetDirection()) {
            case TextDir::Inherit: name = "inherit"; break;
            case TextDir::Rtl: name = "rtl"; break;
            default: break;
        }
    }
    info.GetReturnValue().Set(Nan::New(name).ToLocalChecked());
}

// --- Form ---------------------------------------------------------------------

void NewForm(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::Form>(info, static_cast<int>(NumArg(info, 0, 1)),
                         static_cast<float>(NumArg(info, 1, -1.0)));
}

NAN_METHOD(FormSetColumns) {
    auto* w = CastWidget<UIW::Form>(info.This());
    if (w) w->SetColumns(static_cast<int>(NumArg(info, 0, w->GetColumns())));
}

NAN_METHOD(FormGetColumns) {
    auto* w = CastWidget<UIW::Form>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetColumns() : 1));
}

NAN_METHOD(FormSetGap) {
    auto* w = CastWidget<UIW::Form>(info.This());
    if (w) w->SetGap(static_cast<float>(NumArg(info, 0, w->GetGap())));
}

NAN_METHOD(FormGetGap) {
    auto* w = CastWidget<UIW::Form>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetGap() : -1.0));
}

// --- Attachment ---------------------------------------------------------------

void NewAttachment(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Attachment>(
                                 StrArg(info, 0), StrArg(info, 1)));
}

NAN_METHOD(AttachmentSetFilename) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetFilename(std::move(v));
}

NAN_METHOD(AttachmentGetFilename) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetFilename()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(AttachmentSetMeta) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetMeta(std::move(v));
}

NAN_METHOD(AttachmentGetMeta) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetMeta()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(AttachmentSetOnRemove) {
    auto* w = CastWidget<UIW::Attachment>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "remove",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnRemove(EmitFn<void>(store, "remove"));
                       });
}

// --- Bubble / Message -----------------------------------------------------------

using BubbleAlign = UIW::Bubble::Align;

BubbleAlign AlignArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                     int index) {
    return EnumArg(info, index, BubbleAlign::Other,
                   {{"Other", BubbleAlign::Other}, {"Own", BubbleAlign::Own}});
}

void NewBubble(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Bubble>(
                                 StrArg(info, 0), AlignArg(info, 1)));
}

NAN_METHOD(BubbleSetText) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetText(std::move(v));
}

NAN_METHOD(BubbleGetText) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(BubbleSetAlign) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    if (w) w->SetAlign(AlignArg(info, 0));
}

NAN_METHOD(BubbleGetAlign) {
    auto* w = CastWidget<UIW::Bubble>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->GetAlign() == BubbleAlign::Own ? "own" : "other")
            .ToLocalChecked());
}

void NewMessage(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Message>(
                                 StrArg(info, 0), StrArg(info, 1),
                                 StrArg(info, 2), AlignArg(info, 3)));
}

NAN_METHOD(MessageSetSender) {
    auto* w = CastWidget<UIW::Message>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetSender(std::move(v));
}

NAN_METHOD(MessageGetSender) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetSender()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(MessageSetText) {
    auto* w = CastWidget<UIW::Message>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetText(std::move(v));
}

NAN_METHOD(MessageGetText) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(MessageSetTime) {
    auto* w = CastWidget<UIW::Message>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTime(std::move(v));
}

NAN_METHOD(MessageGetTime) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetTime()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(MessageSetAlign) {
    auto* w = CastWidget<UIW::Message>(info.This());
    if (w) w->SetAlign(AlignArg(info, 0));
}

NAN_METHOD(MessageGetAlign) {
    auto* w = CastWidget<UIW::Message>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->GetAlign() == BubbleAlign::Own ? "own" : "other")
            .ToLocalChecked());
}

// --- MessageScroller ------------------------------------------------------------

void NewMessageScroller(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::MessageScroller>());
}

NAN_METHOD(MessageScrollerAddMessage) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    if (!w) return;
    auto child = WidgetFromArg(info, 0);
    if (child) w->AddMessage(std::move(child));
}

NAN_METHOD(MessageScrollerClear) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    if (w) w->Clear();
}

NAN_METHOD(MessageScrollerScrollToBottom) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    if (w) w->ScrollToBottom();
}

NAN_METHOD(MessageScrollerGetScrollY) {
    auto* w = CastWidget<UIW::MessageScroller>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetScrollY() : 0.0));
}

// --- InputOTP -------------------------------------------------------------------

void NewInputOTP(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    NewSimple<UIW::InputOTP>(
        info, static_cast<int>(NumArg(info, 0, 6)));
}

NAN_METHOD(InputOTPSetLength) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    if (w) w->SetLength(static_cast<int>(NumArg(info, 0, w->GetLength())));
}

NAN_METHOD(InputOTPGetLength) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetLength() : 6));
}

NAN_METHOD(InputOTPSetCode) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetCode(std::move(v));
}

NAN_METHOD(InputOTPGetCode) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetCode()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(InputOTPSetOnComplete) {
    auto* w = CastWidget<UIW::InputOTP>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "complete",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnComplete(
                               EmitFn<void, std::string>(store, "complete"));
                       });
}

// --- HoverCard -------------------------------------------------------------------

void NewHoverCard(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::HoverCard>());
}

NAN_METHOD(HoverCardSetTrigger) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (!w) return;
    auto trigger = WidgetFromArg(info, 0);
    if (trigger) w->SetTrigger(std::move(trigger));
}

NAN_METHOD(HoverCardSetTitle) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTitle(std::move(v));
}

NAN_METHOD(HoverCardGetTitle) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetTitle()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(HoverCardSetDescription) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetDescription(std::move(v));
}

NAN_METHOD(HoverCardGetDescription) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetDescription()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(HoverCardSetContent) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (!w) return;
    auto content = WidgetFromArg(info, 0);
    if (content) w->SetContent(std::move(content));
}

NAN_METHOD(HoverCardSetOpenDelay) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (w) w->SetOpenDelay(static_cast<float>(NumArg(info, 0, 0.5)));
}

NAN_METHOD(HoverCardSetActionLabel) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetActionLabel(std::move(v));
}

NAN_METHOD(HoverCardSetOnAction) {
    auto* w = CastWidget<UIW::HoverCard>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "action",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnAction(EmitFn<void>(store, "action"));
                       });
}

// --- Popover -----------------------------------------------------------------

using PopSide = UIW::Popover::Side;
using PopAlign = UIW::Popover::Align;

PopSide PopSideArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                   int index) {
    return EnumArg(info, index, PopSide::Bottom,
                   {{"Bottom", PopSide::Bottom}, {"Top", PopSide::Top},
                    {"Left", PopSide::Left}, {"Right", PopSide::Right}});
}

PopAlign PopAlignArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                     int index) {
    return EnumArg(info, index, PopAlign::Start,
                   {{"Start", PopAlign::Start}, {"Center", PopAlign::Center},
                    {"End", PopAlign::End}});
}

void NewPopover(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Popover>());
}

NAN_METHOD(PopoverSetTrigger) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (!w) return;
    auto trigger = WidgetFromArg(info, 0);
    if (trigger) w->SetTrigger(std::move(trigger));
}

NAN_METHOD(PopoverSetContent) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (!w) return;
    auto content = WidgetFromArg(info, 0);
    if (content) w->SetContent(std::move(content));
}

NAN_METHOD(PopoverSetTitle) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTitle(std::move(v));
}

NAN_METHOD(PopoverGetTitle) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetTitle()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(PopoverSetSide) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (w) w->SetSide(PopSideArg(info, 0));
}

NAN_METHOD(PopoverSetAlign) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (w) w->SetAlign(PopAlignArg(info, 0));
}

NAN_METHOD(PopoverSetOpen) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (w) w->SetOpen(BoolOr(info[0], false));
}

NAN_METHOD(PopoverIsOpen) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsOpen()));
}

NAN_METHOD(PopoverOpen) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (w) w->Open();
}

NAN_METHOD(PopoverClose) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (w) w->Close();
}

NAN_METHOD(PopoverSetOnOpenChange) {
    auto* w = CastWidget<UIW::Popover>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "openChange",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnOpenChange(
                               EmitFn<void, bool>(store, "openChange"));
                       });
}

// --- DropdownMenu ------------------------------------------------------------

// Same item shape as ContextMenu: {label, action?, enabled?, checked?,
// shortcut?, separatorBefore?, danger?}
UIW::DropdownMenu::Item DropdownItemFromJS(
    v8::Local<v8::Object> o, std::shared_ptr<CallbackStore> store) {
    UIW::DropdownMenu::Item item;
    item.label = xer::addon::StrProp(o, "label");
    item.enabled = xer::addon::BoolProp(o, "enabled", true);
    item.checked = xer::addon::BoolProp(o, "checked", false);
    item.danger = xer::addon::BoolProp(o, "danger", false);
    item.separator_before = xer::addon::BoolProp(
        o, "separatorBefore",
        xer::addon::BoolProp(o, "separator_before", false));
    item.shortcut = xer::addon::StrProp(o, "shortcut");
    v8::Local<v8::Value> action = xer::addon::GetProp(o, "action");
    if (action->IsFunction()) {
        const std::string key = store->Unique("item");
        store->Set(key.c_str(), action.As<v8::Function>());
        item.action = [store, key]() { store->Emit(key.c_str()); };
    }
    return item;
}

void NewDropdownMenu(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::DropdownMenu>());
}

NAN_METHOD(DropdownMenuSetTrigger) {
    auto* w = CastWidget<UIW::DropdownMenu>(info.This());
    if (!w) return;
    auto trigger = WidgetFromArg(info, 0);
    if (trigger) w->SetTrigger(std::move(trigger));
}

NAN_METHOD(DropdownMenuSetItems) {
    auto* w = CastWidget<UIW::DropdownMenu>(info.This());
    if (!w || !HasArg(info, 0) || !info[0]->IsArray()) return;
    auto store = xer::addon::ui::CallbacksOf(info.This());
    if (!store) return;
    v8::Local<v8::Array> arr = info[0].As<v8::Array>();
    const uint32_t n = arr->Length();
    std::vector<UIW::DropdownMenu::Item> items;
    items.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        v8::Local<v8::Value> el;
        arr->Get(Nan::GetCurrentContext(), i).ToLocal(&el);
        if (el.IsEmpty()) el = Nan::Undefined();
        if (el->IsObject())
            items.push_back(DropdownItemFromJS(el.As<v8::Object>(), store));
    }
    w->SetItems(std::move(items));
}

NAN_METHOD(DropdownMenuOpen) {
    auto* w = CastWidget<UIW::DropdownMenu>(info.This());
    if (w) w->Open();
}

NAN_METHOD(DropdownMenuClose) {
    auto* w = CastWidget<UIW::DropdownMenu>(info.This());
    if (w) w->Close();
}

NAN_METHOD(DropdownMenuIsOpen) {
    auto* w = CastWidget<UIW::DropdownMenu>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsOpen()));
}

NAN_METHOD(DropdownMenuSetOnOpenChange) {
    auto* w = CastWidget<UIW::DropdownMenu>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "openChange",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnOpenChange(
                               EmitFn<void, bool>(store, "openChange"));
                       });
}

// --- Sheet -------------------------------------------------------------------

using SheetSide = UIW::Sheet::Side;

SheetSide SheetSideArg(const Nan::FunctionCallbackInfo<v8::Value>& info,
                       int index) {
    return EnumArg(info, index, SheetSide::Right,
                   {{"Left", SheetSide::Left}, {"Right", SheetSide::Right},
                    {"Top", SheetSide::Top}, {"Bottom", SheetSide::Bottom}});
}

void NewSheet(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto content = WidgetFromArg(info, 1);
    WidgetWrap::Adopt(info, UIW::Sheet::Create(
        StrArg(info, 0), content, SheetSideArg(info, 2)));
}

NAN_METHOD(SheetSetTitle) {
    auto* w = CastWidget<UIW::Sheet>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTitle(std::move(v));
}

NAN_METHOD(SheetSetContent) {
    auto* w = CastWidget<UIW::Sheet>(info.This());
    if (!w) return;
    auto content = WidgetFromArg(info, 0);
    if (content) w->SetContent(std::move(content));
}

NAN_METHOD(SheetSetSide) {
    auto* w = CastWidget<UIW::Sheet>(info.This());
    if (w) w->SetSide(SheetSideArg(info, 0));
}

NAN_METHOD(SheetSetPanelSize) {
    auto* w = CastWidget<UIW::Sheet>(info.This());
    if (w) w->SetPanelSize(static_cast<float>(NumArg(info, 0, 420.0)));
}

NAN_METHOD(SheetSetOnClosed) {
    auto* w = CastWidget<UIW::Sheet>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "closed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnClosed(EmitFn<void>(store, "closed"));
                       });
}

NAN_METHOD(SheetClose) {
    auto* w = CastWidget<UIW::Sheet>(info.This());
    if (w) w->Close();
}

// --- Drawer ------------------------------------------------------------------

void NewDrawer(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto content = WidgetFromArg(info, 1);
    WidgetWrap::Adopt(info,
                      UIW::Drawer::Create(StrArg(info, 0), content));
}

NAN_METHOD(DrawerSetTitle) {
    auto* w = CastWidget<UIW::Drawer>(info.This());
    std::string v;
    if (w && ArgUtf8(info, 0, &v)) w->SetTitle(std::move(v));
}

NAN_METHOD(DrawerSetContent) {
    auto* w = CastWidget<UIW::Drawer>(info.This());
    if (!w) return;
    auto content = WidgetFromArg(info, 0);
    if (content) w->SetContent(std::move(content));
}

NAN_METHOD(DrawerSetSnapPoint) {
    auto* w = CastWidget<UIW::Drawer>(info.This());
    if (w) w->SetSnapPoint(static_cast<float>(NumArg(info, 0, 0.5)));
}

NAN_METHOD(DrawerSetOnClosed) {
    auto* w = CastWidget<UIW::Drawer>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "closed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnClosed(EmitFn<void>(store, "closed"));
                       });
}

NAN_METHOD(DrawerClose) {
    auto* w = CastWidget<UIW::Drawer>(info.This());
    if (w) w->Close();
}

// --- Resizable ---------------------------------------------------------------

using ResDir = UIW::Resizable::Direction;

void NewResizable(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    const auto dir = EnumArg(info, 0, ResDir::Horizontal,
                             {{"Horizontal", ResDir::Horizontal},
                              {"Vertical", ResDir::Vertical}});
    WidgetWrap::Adopt(info, UIW::Resizable::Create(dir));
}

// JS panel: {widget, size?, minSize?, collapsible?}
void ResizableAddPanel(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    auto* w = CastWidget<UIW::Resizable>(info.This());
    if (!w || !HasArg(info, 0) || !info[0]->IsObject()) return;
    v8::Local<v8::Object> o = info[0].As<v8::Object>();
    UIW::Resizable::Panel panel;
    v8::Local<v8::Value> widget = xer::addon::GetProp(o, "widget");
    if (!widget.IsEmpty()) {
        panel.widget = xer::addon::ui::WidgetFromValue(widget);
    }
    if (!panel.widget) return;
    panel.size = static_cast<float>(xer::addon::NumberOr(
        xer::addon::GetProp(o, "size"), 0.0));
    panel.min_size = static_cast<float>(xer::addon::NumberOr(
        xer::addon::GetProp(o, "minSize"), 0.0));
    panel.collapsible = xer::addon::BoolProp(o, "collapsible", true);
    w->AddPanel(std::move(panel));
}

NAN_METHOD(ResizableClearPanels) {
    auto* w = CastWidget<UIW::Resizable>(info.This());
    if (w) w->ClearPanels();
}

NAN_METHOD(ResizableSetDirection) {
    auto* w = CastWidget<UIW::Resizable>(info.This());
    if (!w) return;
    const auto dir = EnumArg(info, 0, ResDir::Horizontal,
                             {{"Horizontal", ResDir::Horizontal},
                              {"Vertical", ResDir::Vertical}});
    w->SetDirection(dir);
}

NAN_METHOD(ResizableSetHandleSize) {
    auto* w = CastWidget<UIW::Resizable>(info.This());
    if (w) w->SetHandleSize(static_cast<float>(NumArg(info, 0, 6.0)));
}

NAN_METHOD(ResizableSetCollapsed) {
    auto* w = CastWidget<UIW::Resizable>(info.This());
    if (w) {
        w->SetCollapsed(static_cast<int>(NumArg(info, 0, 0)),
                        BoolOr(info[1], false));
    }
}

NAN_METHOD(ResizableSetOnLayout) {
    auto* w = CastWidget<UIW::Resizable>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "layout",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnLayout(
                               [store](const std::vector<float>& sizes) {
                                   store->Emit("layout", sizes);
                               });
                       });
}

// --- Carousel ----------------------------------------------------------------

void NewCarousel(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, UIW::Carousel::Create());
}

NAN_METHOD(CarouselAddSlide) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (!w) return;
    auto slide = WidgetFromArg(info, 0);
    if (slide) w->AddSlide(std::move(slide));
}

NAN_METHOD(CarouselClearSlides) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->ClearSlides();
}

NAN_METHOD(CarouselSetIndex) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->SetIndex(static_cast<int>(NumArg(info, 0, 0)));
}

NAN_METHOD(CarouselGetIndex) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetIndex() : 0));
}

NAN_METHOD(CarouselGetCount) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetCount() : 0));
}

NAN_METHOD(CarouselSetLoop) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->SetLoop(BoolOr(info[0], true));
}

NAN_METHOD(CarouselSetShowArrows) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->SetShowArrows(BoolOr(info[0], true));
}

NAN_METHOD(CarouselSetShowDots) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->SetShowDots(BoolOr(info[0], true));
}

NAN_METHOD(CarouselNext) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->Next();
}

NAN_METHOD(CarouselPrev) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (w) w->Prev();
}

NAN_METHOD(CarouselSetOnIndexChanged) {
    auto* w = CastWidget<UIW::Carousel>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "indexChanged",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnIndexChanged(
                               EmitFn<void, int>(store, "indexChanged"));
                       });
}

} // namespace

namespace xer::addon::ui {

void RegisterKit(v8::Local<v8::Object> xer) {
    RegisterWidgetClass(xer, "Kbd", NewKbd,
                        {{"SetText", KbdSetText}, {"GetText", KbdGetText}});
    RegisterWidgetClass(xer, "Marker", NewMarker,
                        {{"SetText", MarkerSetText},
                         {"GetText", MarkerGetText}});
    RegisterWidgetClass(
        xer, "AspectRatio", NewAspectRatio,
        {{"SetRatio", AspectRatioSetRatio},
         {"GetRatio", AspectRatioGetRatio}});
    RegisterWidgetClass(
        xer, "ButtonGroup", NewButtonGroup,
        {{"SetOrientation", ButtonGroupSetOrientation},
         {"GetOrientation", ButtonGroupGetOrientation}});
    RegisterWidgetClass(
        xer, "InputGroup", NewSimple<UIW::InputGroup>,
        {{"SetPrefix", InputGroupSetPrefix},
         {"GetPrefix", InputGroupGetPrefix},
         {"SetSuffix", InputGroupSetSuffix},
         {"GetSuffix", InputGroupGetSuffix}});
    RegisterWidgetClass(
        xer, "Field", NewSimple<UIW::Field>,
        {{"SetLabel", FieldSetLabel},
         {"GetLabel", FieldGetLabel},
         {"SetDescription", FieldSetDescription},
         {"GetDescription", FieldGetDescription},
         {"SetError", FieldSetError},
         {"GetError", FieldGetError}});
    RegisterWidgetClass(
        xer, "Item", NewSimple<UIW::Item>,
        {{"SetTitle", ItemSetTitle},
         {"GetTitle", ItemGetTitle},
         {"SetDescription", ItemSetDescription},
         {"GetDescription", ItemGetDescription},
         {"SetSelected", ItemSetSelected},
         {"IsSelected", ItemIsSelected}});
    RegisterWidgetClass(
        xer, "Empty", NewSimple<UIW::Empty>,
        {{"SetTitle", EmptySetTitle},
         {"GetTitle", EmptyGetTitle},
         {"SetDescription", EmptySetDescription},
         {"GetDescription", EmptyGetDescription}});
    RegisterWidgetClass(
        xer, "Collapsible", NewCollapsible,
        {{"SetOpen", CollapsibleSetOpen},
         {"IsOpen", CollapsibleIsOpen},
         {"GetProgress", CollapsibleGetProgress},
         {"SetOnToggled", CollapsibleSetOnToggled}});
    RegisterWidgetClass(
        xer, "Direction", NewDirection,
        {{"SetDirection", DirectionSetDirection},
         {"GetDirection", DirectionGetDirection}});
    RegisterWidgetClass(
        xer, "Form", NewForm,
        {{"SetColumns", FormSetColumns},
         {"GetColumns", FormGetColumns},
         {"SetGap", FormSetGap},
         {"GetGap", FormGetGap}});
    RegisterWidgetClass(
        xer, "Attachment", NewAttachment,
        {{"SetFilename", AttachmentSetFilename},
         {"GetFilename", AttachmentGetFilename},
         {"SetMeta", AttachmentSetMeta},
         {"GetMeta", AttachmentGetMeta},
         {"SetOnRemove", AttachmentSetOnRemove}});
    RegisterWidgetClass(
        xer, "Bubble", NewBubble,
        {{"SetText", BubbleSetText},
         {"GetText", BubbleGetText},
         {"SetAlign", BubbleSetAlign},
         {"GetAlign", BubbleGetAlign}});
    RegisterWidgetClass(
        xer, "Message", NewMessage,
        {{"SetSender", MessageSetSender},
         {"GetSender", MessageGetSender},
         {"SetText", MessageSetText},
         {"GetText", MessageGetText},
         {"SetTime", MessageSetTime},
         {"GetTime", MessageGetTime},
         {"SetAlign", MessageSetAlign},
         {"GetAlign", MessageGetAlign}});
    RegisterWidgetClass(
        xer, "MessageScroller", NewMessageScroller,
        {{"AddMessage", MessageScrollerAddMessage},
         {"Clear", MessageScrollerClear},
         {"ScrollToBottom", MessageScrollerScrollToBottom},
         {"GetScrollY", MessageScrollerGetScrollY}});
    RegisterWidgetClass(
        xer, "InputOTP", NewInputOTP,
        {{"SetLength", InputOTPSetLength},
         {"GetLength", InputOTPGetLength},
         {"SetCode", InputOTPSetCode},
         {"GetCode", InputOTPGetCode},
         {"SetOnComplete", InputOTPSetOnComplete}});
    RegisterWidgetClass(
        xer, "HoverCard", NewHoverCard,
        {{"SetTrigger", HoverCardSetTrigger},
         {"SetTitle", HoverCardSetTitle},
         {"GetTitle", HoverCardGetTitle},
         {"SetDescription", HoverCardSetDescription},
         {"GetDescription", HoverCardGetDescription},
         {"SetContent", HoverCardSetContent},
         {"SetOpenDelay", HoverCardSetOpenDelay},
         {"SetActionLabel", HoverCardSetActionLabel},
         {"SetOnAction", HoverCardSetOnAction}});
    RegisterWidgetClass(
        xer, "Popover", NewPopover,
        {{"SetTrigger", PopoverSetTrigger},
         {"SetContent", PopoverSetContent},
         {"SetTitle", PopoverSetTitle},
         {"GetTitle", PopoverGetTitle},
         {"SetSide", PopoverSetSide},
         {"SetAlign", PopoverSetAlign},
         {"SetOpen", PopoverSetOpen},
         {"IsOpen", PopoverIsOpen},
         {"Open", PopoverOpen},
         {"Close", PopoverClose},
         {"SetOnOpenChange", PopoverSetOnOpenChange}});
    RegisterWidgetClass(
        xer, "DropdownMenu", NewDropdownMenu,
        {{"SetTrigger", DropdownMenuSetTrigger},
         {"SetItems", DropdownMenuSetItems},
         {"Open", DropdownMenuOpen},
         {"Close", DropdownMenuClose},
         {"IsOpen", DropdownMenuIsOpen},
         {"SetOnOpenChange", DropdownMenuSetOnOpenChange}});
    RegisterWidgetClass(
        xer, "Sheet", NewSheet,
        {{"SetTitle", SheetSetTitle},
         {"SetContent", SheetSetContent},
         {"SetSide", SheetSetSide},
         {"SetPanelSize", SheetSetPanelSize},
         {"SetOnClosed", SheetSetOnClosed},
         {"Close", SheetClose}});
    RegisterWidgetClass(
        xer, "Drawer", NewDrawer,
        {{"SetTitle", DrawerSetTitle},
         {"SetContent", DrawerSetContent},
         {"SetSnapPoint", DrawerSetSnapPoint},
         {"SetOnClosed", DrawerSetOnClosed},
         {"Close", DrawerClose}});
    RegisterWidgetClass(
        xer, "Resizable", NewResizable,
        {{"AddPanel", ResizableAddPanel},
         {"ClearPanels", ResizableClearPanels},
         {"SetDirection", ResizableSetDirection},
         {"SetHandleSize", ResizableSetHandleSize},
         {"SetCollapsed", ResizableSetCollapsed},
         {"SetOnLayout", ResizableSetOnLayout}});
    RegisterWidgetClass(
        xer, "Carousel", NewCarousel,
        {{"AddSlide", CarouselAddSlide},
         {"ClearSlides", CarouselClearSlides},
         {"SetIndex", CarouselSetIndex},
         {"GetIndex", CarouselGetIndex},
         {"GetCount", CarouselGetCount},
         {"SetLoop", CarouselSetLoop},
         {"SetShowArrows", CarouselSetShowArrows},
         {"SetShowDots", CarouselSetShowDots},
         {"Next", CarouselNext},
         {"Prev", CarouselPrev},
         {"SetOnIndexChanged", CarouselSetOnIndexChanged}});
}

} // namespace xer::addon::ui
