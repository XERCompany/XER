#include "flux/terminal/terminal_manager.h"
#include "flux/terminal/terminal_interface.h"

namespace XER::Flux::Terminal {

TerminalManager& TerminalManager::Instance() {
	static TerminalManager manager;
	return manager;
}

void TerminalManager::Write(OutputStream stream, std::string_view text) {
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.Append(stream, text);
	GetDefaultTerminal().Write(stream, text);
}

void TerminalManager::WriteLine(OutputStream stream, std::string_view text) {
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.AppendLine(stream, text);
	TerminalInterface& terminal = GetDefaultTerminal();
	terminal.Write(stream, text);
	terminal.Write(stream, "\n");
	terminal.Flush(stream);
}

void TerminalManager::Flush(OutputStream stream) {
	std::lock_guard<std::mutex> lock(mutex_);
	GetDefaultTerminal().Flush(stream);
}

void TerminalManager::ResetBuffer() {
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.Clear();
}

void TerminalManager::ResetBuffer(OutputStream stream) {
	std::lock_guard<std::mutex> lock(mutex_);
	buffer_.Clear(stream);
}

std::string TerminalManager::Snapshot(OutputStream stream) const {
	std::lock_guard<std::mutex> lock(mutex_);
	return buffer_.Snapshot(stream);
}

std::string TerminalManager::SnapshotPlainText(OutputStream stream) const {
	std::lock_guard<std::mutex> lock(mutex_);
	return buffer_.SnapshotPlainText(stream);
}

const TerminalBuffer& TerminalManager::buffer() const {
	return buffer_;
}

}  // namespace XER::Flux::Terminal
