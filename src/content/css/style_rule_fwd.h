/**
 * style_rule_fwd.h — forward declaration of StyleDeclaration so
 * computed_style.h does not need the full rule header.
 */
#pragma once

#include <string>

namespace XER::Content::CSS {

struct StyleDeclaration {
    std::string property;
    std::string value;
};

}  // namespace XER::Content::CSS
