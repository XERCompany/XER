#pragma once

#include <mutex>
#include <string>
#include <string_view>

#include "flux/terminal/terminal_buffer.h"

namespace XER::Flux::Terminal {

class TerminalManager {
public:
	static TerminalManager& Instance();

	void Write(OutputStream stream, std::string_view text);
	void WriteLine(OutputStream stream, std::string_view text);
	void Flush(OutputStream stream);
	void ResetBuffer();
	void ResetBuffer(OutputStream stream);
	std::string Snapshot(OutputStream stream) const;
	std::string SnapshotPlainText(OutputStream stream) const;

	const TerminalBuffer& buffer() const;

private:
	TerminalManager() = default;

	mutable std::mutex mutex_;
	TerminalBuffer buffer_;
};

}  // namespace XER::Flux::Terminal
