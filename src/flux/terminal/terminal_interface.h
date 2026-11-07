#pragma once

#include <memory>
#include <string_view>

namespace XER::flux::terminal {

enum class OutputStream {
	kStdout,
	kStderr,
};

class TerminalInterface {
public:
	virtual ~TerminalInterface() = default;

	virtual void Write(OutputStream stream, std::string_view text) = 0;
	virtual void Flush(OutputStream stream) = 0;
};

TerminalInterface& GetDefaultTerminal();
void SetDefaultTerminal(std::unique_ptr<TerminalInterface> terminal);

}  // namespace XER::flux::terminal
