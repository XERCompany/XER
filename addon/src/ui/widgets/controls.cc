// xer.UI.* — input controls: Button, Checkbox, RadioGroup, Select, Slider,
// Switch, Tabs, Toggle, ToggleGroup, TextInput, Textarea, Pagination.

#include "../widget.h"
#include "../../convert.h"

#include <string>
#include <vector>

#include "widgets/button.h"
#include "widgets/checkbox.h"
#include "widgets/radio_group.h"
#include "widgets/select.h"
#include "widgets/slider.h"
#include "widgets/switch.h"
#include "widgets/tabs.h"
#include "widgets/toggle.h"
#include "widgets/text_input.h"
#include "widgets/textarea.h"
#include "widgets/pagination.h"

namespace {

namespace UIW = XER::UI::Widgets;
using xer::addon::HasArg;
using xer::addon::ArgUtf8;
using xer::addon::EnumArg;
using xer::addon::NumberOr;
using xer::addon::BoolOr;
using xer::addon::StringArray;
using xer::addon::IntArray;
using xer::addon::ui::WidgetWrap;
using xer::addon::CallbackStore;
using xer::addon::ui::CastWidget;
using xer::addon::ui::CallbacksOf;
using xer::addon::ui::BindWidgetCallback;
using xer::addon::ui::RegisterWidgetClass;

// --- small shared helpers -----------------------------------------------------

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

// --- Button -------------------------------------------------------------------

void NewButton(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Button>(
        StrArg(info, 0),
        EnumArg(info, 1, UIW::ButtonStyle::Secondary,
                {{"primary", UIW::ButtonStyle::Primary},
                 {"secondary", UIW::ButtonStyle::Secondary},
                 {"outline", UIW::ButtonStyle::Outline},
                 {"ghost", UIW::ButtonStyle::Ghost},
                 {"destructive", UIW::ButtonStyle::Destructive}})));
}

NAN_METHOD(ButtonSetText) {
    auto* w = CastWidget<UIW::Button>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetText(text);
}

NAN_METHOD(ButtonGetText) {
    auto* w = CastWidget<UIW::Button>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(ButtonSetStyle) {
    auto* w = CastWidget<UIW::Button>(info.This());
    if (!w) return;
    w->SetStyle(EnumArg(info, 0, UIW::ButtonStyle::Secondary,
                        {{"primary", UIW::ButtonStyle::Primary},
                         {"secondary", UIW::ButtonStyle::Secondary},
                         {"outline", UIW::ButtonStyle::Outline},
                         {"ghost", UIW::ButtonStyle::Ghost},
                         {"destructive", UIW::ButtonStyle::Destructive}}));
}

NAN_METHOD(ButtonSetLoading) {
    auto* w = CastWidget<UIW::Button>(info.This());
    if (w) w->SetLoading(BoolArg(info, 0, false));
}

NAN_METHOD(ButtonIsLoading) {
    auto* w = CastWidget<UIW::Button>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsLoading()));
}

NAN_METHOD(ButtonSetOnClick) {
    auto* w = CastWidget<UIW::Button>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "click",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnClick(store ? [store]() {
                               store->Emit("click");
                           } : nullptr);
                       });
}

// --- Checkbox -----------------------------------------------------------------

void NewCheckbox(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info,
                      std::make_shared<UIW::Checkbox>(BoolArg(info, 0, false)));
}

NAN_METHOD(CheckboxIsChecked) {
    auto* w = CastWidget<UIW::Checkbox>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsChecked()));
}

NAN_METHOD(CheckboxSetChecked) {
    auto* w = CastWidget<UIW::Checkbox>(info.This());
    if (w) w->SetChecked(BoolArg(info, 0, false));
}

NAN_METHOD(CheckboxSetOnChanged) {
    auto* w = CastWidget<UIW::Checkbox>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](bool v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- RadioGroup ---------------------------------------------------------------

void NewRadioGroup(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::RadioGroup>());
}

NAN_METHOD(RadioGroupSetOptions) {
    auto* w = CastWidget<UIW::RadioGroup>(info.This());
    if (w && HasArg(info, 0)) w->SetOptions(StringArray(info[0]));
}

NAN_METHOD(RadioGroupSetSelectedIndex) {
    auto* w = CastWidget<UIW::RadioGroup>(info.This());
    if (w) w->SetSelectedIndex(static_cast<int>(NumArg(info, 0, -1)));
}

NAN_METHOD(RadioGroupGetSelectedIndex) {
    auto* w = CastWidget<UIW::RadioGroup>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetSelectedIndex() : -1));
}

NAN_METHOD(RadioGroupSetOnChanged) {
    auto* w = CastWidget<UIW::RadioGroup>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](int v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- Select -------------------------------------------------------------------

void NewSelect(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Select>(
        HasArg(info, 0) ? StringArray(info[0]) : std::vector<std::string>{},
        static_cast<int>(NumArg(info, 1, -1))));
}

NAN_METHOD(SelectSetOptions) {
    auto* w = CastWidget<UIW::Select>(info.This());
    if (w && HasArg(info, 0)) w->SetOptions(StringArray(info[0]));
}

NAN_METHOD(SelectSetSelectedIndex) {
    auto* w = CastWidget<UIW::Select>(info.This());
    if (w) w->SetSelectedIndex(static_cast<int>(NumArg(info, 0, -1)));
}

NAN_METHOD(SelectGetSelectedIndex) {
    auto* w = CastWidget<UIW::Select>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetSelectedIndex() : -1));
}

NAN_METHOD(SelectGetSelectedText) {
    auto* w = CastWidget<UIW::Select>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetSelectedText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(SelectSetPlaceholder) {
    auto* w = CastWidget<UIW::Select>(info.This());
    std::string s;
    if (w && ArgUtf8(info, 0, &s)) w->SetPlaceholder(s);
}

NAN_METHOD(SelectIsDropdownOpen) {
    auto* w = CastWidget<UIW::Select>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsDropdownOpen()));
}

NAN_METHOD(SelectSetOnChanged) {
    auto* w = CastWidget<UIW::Select>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](int v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- Slider -------------------------------------------------------------------

void NewSlider(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Slider>(
        static_cast<float>(NumArg(info, 0, 0.0)),
        static_cast<float>(NumArg(info, 1, 1.0)),
        static_cast<float>(NumArg(info, 2, 0.0))));
}

NAN_METHOD(SliderSetRange) {
    auto* w = CastWidget<UIW::Slider>(info.This());
    if (w) w->SetRange(static_cast<float>(NumArg(info, 0, 0.0)),
                       static_cast<float>(NumArg(info, 1, 1.0)));
}

NAN_METHOD(SliderSetValue) {
    auto* w = CastWidget<UIW::Slider>(info.This());
    if (w) w->SetValue(static_cast<float>(NumArg(info, 0, 0.0)));
}

NAN_METHOD(SliderGetValue) {
    auto* w = CastWidget<UIW::Slider>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? static_cast<double>(w->GetValue())
                                        : 0.0));
}

NAN_METHOD(SliderIsDragging) {
    auto* w = CastWidget<UIW::Slider>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsDragging()));
}

NAN_METHOD(SliderSetOnValueChanged) {
    auto* w = CastWidget<UIW::Slider>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "valueChanged",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnValueChanged(store ? [store](float v) {
                               store->Emit("valueChanged", v);
                           } : nullptr);
                       });
}

// --- Switch -------------------------------------------------------------------

void NewSwitch(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(
        info, std::make_shared<UIW::Switch>(BoolArg(info, 0, false)));
}

NAN_METHOD(SwitchIsChecked) {
    auto* w = CastWidget<UIW::Switch>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsChecked()));
}

NAN_METHOD(SwitchSetChecked) {
    auto* w = CastWidget<UIW::Switch>(info.This());
    if (w) w->SetChecked(BoolArg(info, 0, false));
}

NAN_METHOD(SwitchSetOnChanged) {
    auto* w = CastWidget<UIW::Switch>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](bool v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- Tabs ---------------------------------------------------------------------

void NewTabs(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Tabs>());
}

NAN_METHOD(TabsAddTab) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    if (w) w->AddTab(StrArg(info, 0));
}

NAN_METHOD(TabsClearTabs) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    if (w) w->ClearTabs();
}

NAN_METHOD(TabsGetActiveIndex) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetActiveIndex() : -1));
}

NAN_METHOD(TabsSetActiveIndex) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    if (w) w->SetActiveIndex(static_cast<int>(NumArg(info, 0, -1)));
}

NAN_METHOD(TabsSetStyle) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    if (!w) return;
    w->SetStyle(EnumArg(info, 0, UIW::Tabs::Style::Pill,
                        {{"pill", UIW::Tabs::Style::Pill},
                         {"underline", UIW::Tabs::Style::Underline}}));
}

NAN_METHOD(TabsGetStyle) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w ? static_cast<int>(w->GetStyle()) : 0));
}

NAN_METHOD(TabsSetOnChanged) {
    auto* w = CastWidget<UIW::Tabs>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](int v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- Toggle -------------------------------------------------------------------

void NewToggle(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Toggle>(
        StrArg(info, 0), BoolArg(info, 1, false)));
}

NAN_METHOD(ToggleSetText) {
    auto* w = CastWidget<UIW::Toggle>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetText(text);
}

NAN_METHOD(ToggleGetText) {
    auto* w = CastWidget<UIW::Toggle>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(ToggleSetPressed) {
    auto* w = CastWidget<UIW::Toggle>(info.This());
    if (w) w->SetPressed(BoolArg(info, 0, false));
}

NAN_METHOD(ToggleIsPressed) {
    auto* w = CastWidget<UIW::Toggle>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->IsPressed()));
}

NAN_METHOD(ToggleSetOnPressedChanged) {
    auto* w = CastWidget<UIW::Toggle>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "pressedChanged",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnPressedChanged(store ? [store](bool v) {
                               store->Emit("pressedChanged", v);
                           } : nullptr);
                       });
}

// --- ToggleGroup --------------------------------------------------------------

void NewToggleGroup(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::ToggleGroup>());
}

NAN_METHOD(ToggleGroupSetOptions) {
    auto* w = CastWidget<UIW::ToggleGroup>(info.This());
    if (w && HasArg(info, 0))
        w->SetOptions(StringArray(info[0]), BoolArg(info, 1, false));
}

NAN_METHOD(ToggleGroupSetSelectedIndices) {
    auto* w = CastWidget<UIW::ToggleGroup>(info.This());
    if (w && HasArg(info, 0)) w->SetSelectedIndices(IntArray(info[0]));
}

NAN_METHOD(ToggleGroupGetSelectedIndices) {
    auto* w = CastWidget<UIW::ToggleGroup>(info.This());
    if (!w) {
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(
        xer::addon::ToV8(w->GetSelectedIndices()));
}

NAN_METHOD(ToggleGroupIsSelected) {
    auto* w = CastWidget<UIW::ToggleGroup>(info.This());
    info.GetReturnValue().Set(
        Nan::New(w && w->IsSelected(static_cast<int>(NumArg(info, 0, -1)))));
}

NAN_METHOD(ToggleGroupSetOnChanged) {
    auto* w = CastWidget<UIW::ToggleGroup>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](const std::vector<int>& v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- TextInput ----------------------------------------------------------------

void NewTextInput(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info,
                      std::make_shared<UIW::TextInput>(StrArg(info, 0)));
}

NAN_METHOD(TextInputGetValue) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetValue()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(TextInputSetValue) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetValue(text);
}

NAN_METHOD(TextInputSetPlaceholder) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetPlaceholder(text);
}

NAN_METHOD(TextInputSetPasswordMode) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    if (w) w->SetPasswordMode(BoolArg(info, 0, false));
}

NAN_METHOD(TextInputUndo) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->Undo()));
}

NAN_METHOD(TextInputRedo) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    info.GetReturnValue().Set(Nan::New(w && w->Redo()));
}

NAN_METHOD(TextInputSetOnValueChanged) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "valueChanged",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnValueChanged(store ? [store](const std::string& v) {
                               store->Emit("valueChanged", v);
                           } : nullptr);
                       });
}

NAN_METHOD(TextInputSetOnSubmit) {
    auto* w = CastWidget<UIW::TextInput>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "submit",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnSubmit(store ? [store]() {
                               store->Emit("submit");
                           } : nullptr);
                       });
}

// --- Textarea -----------------------------------------------------------------

void NewTextarea(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info,
                      std::make_shared<UIW::Textarea>(StrArg(info, 0)));
}

NAN_METHOD(TextareaSetText) {
    auto* w = CastWidget<UIW::Textarea>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetText(text);
}

NAN_METHOD(TextareaGetText) {
    auto* w = CastWidget<UIW::Textarea>(info.This());
    info.GetReturnValue().Set(
        w ? Nan::New(w->GetText()).ToLocalChecked()
          : Nan::New("").ToLocalChecked());
}

NAN_METHOD(TextareaSetPlaceholder) {
    auto* w = CastWidget<UIW::Textarea>(info.This());
    std::string text;
    if (w && ArgUtf8(info, 0, &text)) w->SetPlaceholder(text);
}

NAN_METHOD(TextareaSetReadOnly) {
    auto* w = CastWidget<UIW::Textarea>(info.This());
    if (w) w->SetReadOnly(BoolArg(info, 0, false));
}

NAN_METHOD(TextareaSetOnChanged) {
    auto* w = CastWidget<UIW::Textarea>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "changed",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnChanged(store ? [store](const std::string& v) {
                               store->Emit("changed", v);
                           } : nullptr);
                       });
}

// --- Pagination ---------------------------------------------------------------

void NewPagination(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    WidgetWrap::Adopt(info, std::make_shared<UIW::Pagination>(
        static_cast<int>(NumArg(info, 0, 1)),
        static_cast<int>(NumArg(info, 1, 0))));
}

NAN_METHOD(PaginationSetPageCount) {
    auto* w = CastWidget<UIW::Pagination>(info.This());
    if (w) w->SetPageCount(static_cast<int>(NumArg(info, 0, 1)));
}

NAN_METHOD(PaginationSetCurrentPage) {
    auto* w = CastWidget<UIW::Pagination>(info.This());
    if (w) w->SetCurrentPage(static_cast<int>(NumArg(info, 0, 0)));
}

NAN_METHOD(PaginationGetCurrentPage) {
    auto* w = CastWidget<UIW::Pagination>(info.This());
    info.GetReturnValue().Set(Nan::New(w ? w->GetCurrentPage() : 0));
}

NAN_METHOD(PaginationVisiblePages) {
    auto* w = CastWidget<UIW::Pagination>(info.This());
    if (!w) {
        info.GetReturnValue().Set(Nan::Undefined());
        return;
    }
    info.GetReturnValue().Set(xer::addon::ToV8(w->VisiblePages()));
}

NAN_METHOD(PaginationSetOnPageChanged) {
    auto* w = CastWidget<UIW::Pagination>(info.This());
    if (!w) return;
    BindWidgetCallback(info.This(), info, 0, "pageChanged",
                       [&](std::shared_ptr<CallbackStore> store) {
                           w->SetOnPageChanged(store ? [store](int v) {
                               store->Emit("pageChanged", v);
                           } : nullptr);
                       });
}

}  // namespace

namespace xer::addon::ui {

void RegisterControls(v8::Local<v8::Object> xer) {
    RegisterWidgetClass(xer, "Button", NewButton,
                        {{"SetText", ButtonSetText},
                         {"GetText", ButtonGetText},
                         {"SetStyle", ButtonSetStyle},
                         {"SetLoading", ButtonSetLoading},
                         {"IsLoading", ButtonIsLoading},
                         {"SetOnClick", ButtonSetOnClick}});
    RegisterWidgetClass(xer, "Checkbox", NewCheckbox,
                        {{"IsChecked", CheckboxIsChecked},
                         {"SetChecked", CheckboxSetChecked},
                         {"SetOnChanged", CheckboxSetOnChanged}});
    RegisterWidgetClass(xer, "RadioGroup", NewRadioGroup,
                        {{"SetOptions", RadioGroupSetOptions},
                         {"SetSelectedIndex", RadioGroupSetSelectedIndex},
                         {"GetSelectedIndex", RadioGroupGetSelectedIndex},
                         {"SetOnChanged", RadioGroupSetOnChanged}});
    RegisterWidgetClass(xer, "Select", NewSelect,
                        {{"SetOptions", SelectSetOptions},
                         {"SetSelectedIndex", SelectSetSelectedIndex},
                         {"GetSelectedIndex", SelectGetSelectedIndex},
                         {"GetSelectedText", SelectGetSelectedText},
                         {"SetPlaceholder", SelectSetPlaceholder},
                         {"IsDropdownOpen", SelectIsDropdownOpen},
                         {"SetOnChanged", SelectSetOnChanged}});
    RegisterWidgetClass(xer, "Slider", NewSlider,
                        {{"SetRange", SliderSetRange},
                         {"SetValue", SliderSetValue},
                         {"GetValue", SliderGetValue},
                         {"IsDragging", SliderIsDragging},
                         {"SetOnValueChanged", SliderSetOnValueChanged}});
    RegisterWidgetClass(xer, "Switch", NewSwitch,
                        {{"IsChecked", SwitchIsChecked},
                         {"SetChecked", SwitchSetChecked},
                         {"SetOnChanged", SwitchSetOnChanged}});
    RegisterWidgetClass(xer, "Tabs", NewTabs,
                        {{"AddTab", TabsAddTab},
                         {"ClearTabs", TabsClearTabs},
                         {"GetActiveIndex", TabsGetActiveIndex},
                         {"SetActiveIndex", TabsSetActiveIndex},
                         {"SetStyle", TabsSetStyle},
                         {"GetStyle", TabsGetStyle},
                         {"SetOnChanged", TabsSetOnChanged}});
    RegisterWidgetClass(xer, "Toggle", NewToggle,
                        {{"SetText", ToggleSetText},
                         {"GetText", ToggleGetText},
                         {"SetPressed", ToggleSetPressed},
                         {"IsPressed", ToggleIsPressed},
                         {"SetOnPressedChanged", ToggleSetOnPressedChanged}});
    RegisterWidgetClass(xer, "ToggleGroup", NewToggleGroup,
                        {{"SetOptions", ToggleGroupSetOptions},
                         {"SetSelectedIndices", ToggleGroupSetSelectedIndices},
                         {"GetSelectedIndices", ToggleGroupGetSelectedIndices},
                         {"IsSelected", ToggleGroupIsSelected},
                         {"SetOnChanged", ToggleGroupSetOnChanged}});
    RegisterWidgetClass(xer, "TextInput", NewTextInput,
                        {{"GetValue", TextInputGetValue},
                         {"SetValue", TextInputSetValue},
                         {"SetPlaceholder", TextInputSetPlaceholder},
                         {"SetPasswordMode", TextInputSetPasswordMode},
                         {"Undo", TextInputUndo},
                         {"Redo", TextInputRedo},
                         {"SetOnValueChanged", TextInputSetOnValueChanged},
                         {"SetOnSubmit", TextInputSetOnSubmit}});
    RegisterWidgetClass(xer, "Textarea", NewTextarea,
                        {{"SetText", TextareaSetText},
                         {"GetText", TextareaGetText},
                         {"SetPlaceholder", TextareaSetPlaceholder},
                         {"SetReadOnly", TextareaSetReadOnly},
                         {"SetOnChanged", TextareaSetOnChanged}});
    RegisterWidgetClass(xer, "Pagination", NewPagination,
                        {{"SetPageCount", PaginationSetPageCount},
                         {"SetCurrentPage", PaginationSetCurrentPage},
                         {"GetCurrentPage", PaginationGetCurrentPage},
                         {"VisiblePages", PaginationVisiblePages},
                         {"SetOnPageChanged", PaginationSetOnPageChanged}});
}

}  // namespace xer::addon::ui
