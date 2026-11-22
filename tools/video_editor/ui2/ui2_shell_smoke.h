// `ui2_shell` headless smoke check — registered exe-side from main.cc
// next to photo_workspace/widget_kit/etc. Exercises the M1 shell
// without a window: dock structure, workspace preset application and
// the global shortcut router dispatch through a real WidgetTree.
#pragma once

#include <string>

namespace XER::VideoEditor::Ui2 {

// Returns "" on success or a one-line failure reason (Smoke::CheckFn).
std::string Ui2ShellSmoke();

}  // namespace XER::VideoEditor::Ui2
