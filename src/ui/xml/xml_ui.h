#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace XER::UI::Widgets {
class Widget;
}

namespace XER::UI::Xml {

/**
 * XmlUi — declarative widget trees in XML markup ("HTML, but XML").
 *
 * Loads an XML document and instantiates the matching XER widget tree:
 * element names map to widget classes, attributes map to properties,
 * and nested elements become child widgets.
 *
 *   <Card tooltip="profile">
 *     <Form columns="2" gap="8">
 *       <Label text="Name"/>
 *       <TextInput placeholder="Enter name"/>
 *       <Button text="Save" style="primary"/>
 *     </Form>
 *   </Card>
 *
 * Common attributes on every element:
 *   x, y, width, height  absolute DIP bounds (w/h accepted as aliases)
 *   visible, enabled     boolean state
 *   tooltip              hover text
 *   dir                  ltr | rtl | inherit
 *
 * Some containers interpret children specially (documented per tag in
 * xml_ui.cc): Tabs takes <Tab>, Accordion/Sidebar/menus take <Item>,
 * Table takes <Column>/<Row>/<Cell>, Chart takes <Series>, overlays
 * take a <Trigger> or ordered children.
 *
 * The loader never throws: failures are reported through LoadResult.
 * Unknown elements are skipped with a warning (a bad root element is
 * fatal since there is nothing to return); unknown attributes only
 * produce warnings.
 */
struct LoadResult {
    /// The instantiated tree root, or nullptr on failure.
    std::shared_ptr<Widgets::Widget> root;
    /// Non-fatal issues (unknown attributes, ignored children, ...).
    std::vector<std::string> warnings;
    /// Fatal error description (empty on success).
    std::string error;

    bool ok() const { return root != nullptr && error.empty(); }
};

/// Parses `markup` (UTF-8 XML) and builds the widget tree.
LoadResult Load(std::string_view markup);

/// Reads a UTF-8 XML file and builds the widget tree.
LoadResult LoadFile(const std::string& utf8_path);

/// Comma-separated list of tags the loader understands — for tooling
/// and error messages.
std::string SupportedTags();

} // namespace XER::UI::Xml
