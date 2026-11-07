#include "flux/terminal/terminal_output_renderer.h"

#include "flux/terminal/terminal_emulator.h"
#include "flux/terminal/terminal_manager.h"

namespace XER::flux::terminal {

void Write(TerminalInterface& terminal, OutputStream stream, std::string_view text) {
	terminal.Write(stream, text);
}

void Write(OutputStream stream, std::string_view text) {
	TerminalManager::Instance().Write(stream, text);
}

void WriteLine(TerminalInterface& terminal, OutputStream stream, std::string_view text) {
	terminal.Write(stream, text);
	terminal.Write(stream, "\n");
	terminal.Flush(stream);
}

void WriteLine(OutputStream stream, std::string_view text) {
	TerminalManager::Instance().WriteLine(stream, text);
}

void ClearSnapshot() {
	TerminalManager::Instance().ResetBuffer();
}

void ClearSnapshot(OutputStream stream) {
	TerminalManager::Instance().ResetBuffer(stream);
}

std::string Snapshot(OutputStream stream) {
	return TerminalManager::Instance().Snapshot(stream);
}

std::string SnapshotPlainText(OutputStream stream) {
	return TerminalManager::Instance().SnapshotPlainText(stream);
}

}  // namespace XER::flux::terminal
