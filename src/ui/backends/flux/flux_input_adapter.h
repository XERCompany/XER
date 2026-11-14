#ifndef XER_UI_BACKENDS_FLUX_FLUX_INPUT_ADAPTER_H
#define XER_UI_BACKENDS_FLUX_FLUX_INPUT_ADAPTER_H

#include <string_view>

namespace XER::UI::Widgets {
class WidgetTree;
}

namespace XER::UI::Backends {

class FluxInputAdapter {
public:
    bool HandleInput(std::string_view input, Widgets::WidgetTree& tree) const;
};

}  // namespace XER::UI::Backends

#endif  // XER_UI_BACKENDS_FLUX_FLUX_INPUT_ADAPTER_H
