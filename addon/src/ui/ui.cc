// xer.UI.* — UI toolkit namespace entry point.

#include "widget.h"

namespace xer::addon {

void InitUI(v8::Local<v8::Object> exports) {
    ui::RegisterWidgets(exports);
    ui::RegisterControls(exports);
    ui::RegisterDisplay(exports);
    ui::RegisterNav(exports);
    ui::RegisterKit(exports);
    ui::RegisterWindow(exports);
    ui::RegisterApplication(exports);
}

}  // namespace xer::addon
