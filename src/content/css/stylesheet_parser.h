/**
 * StyleSheetParser — parses CSS text into a StyleSheet: "sel, sel2 {
 * prop: value; ... }" blocks. Comma-separated selector lists expand
 * into one StyleRule per alternative (design doc: selector-list
 * alternatives share the declaration block).
 */
#pragma once

#include <string>

#include "content/css/style_rule.h"

namespace XER::Content::CSS {

// Parses |css_text| into a sheet with the given origin.
StyleSheet ParseStyleSheet(const std::string& css_text,
                           CascadeOrigin origin = CascadeOrigin::kAuthor);

}  // namespace XER::Content::CSS
