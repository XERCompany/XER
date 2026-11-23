// Headless smoke hook for the data widgets registered as "data_widgets"
// in main.cc — exercises VirtualList (culling, scroll, selection modes,
// keyboard) and TreeView (flatten, expansion, lazy load + retry,
// tri-state checks, typeahead) without opening a window. Same contract
// as PhotoWorkspaceSmoke/ColorWidgetsSmoke: "" on success, one-line
// failure reason otherwise.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

std::string DataWidgetsSmoke();

}  // namespace XER::VideoEditor::Ui2
