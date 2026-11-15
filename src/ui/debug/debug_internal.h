#pragma once

// Internal helpers shared by debug_map_ui.cc and the DevTools elements
// panel. Not part of the public DebugMapUI API — subject to change.

#include <string>
#include <string_view>
#include <vector>

namespace XER::UI::Widgets {
class Widget;
enum class TextDirection;
}

namespace XER::UI::Debug::internal {

// Demangled type-name components, innermost first
// (e.g. {"Button","Widgets","UI","XER"} for a Button).
std::vector<std::string> TypeComponents(const Widgets::Widget& w);

// "XER::UI::Widgets::Button" style qualified name, outermost first.
std::string QualifiedName(const Widgets::Widget& w);

// Leaf class name sanitized into a valid XML element name.
std::string ElementTag(const Widgets::Widget& w);

std::string DirName(Widgets::TextDirection d);

} // namespace XER::UI::Debug::internal
