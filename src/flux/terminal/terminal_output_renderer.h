#pragma once

#include <string>
#include <string_view>

#include "flux/terminal/terminal_interface.h"

namespace XER::Flux::Terminal {

void Write(TerminalInterface& terminal, OutputStream stream, std::string_view text);
void Write(OutputStream stream, std::string_view text);
void WriteLine(TerminalInterface& terminal, OutputStream stream, std::string_view text);
void WriteLine(OutputStream stream, std::string_view text);
void ClearSnapshot();
void ClearSnapshot(OutputStream stream);
std::string Snapshot(OutputStream stream);
std::string SnapshotPlainText(OutputStream stream);

}  // namespace XER::Flux::Terminal
