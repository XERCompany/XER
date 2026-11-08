#include "flux/terminal/terminal_interface.h"

#include <iostream>
#include <memory>
#include <mutex>

namespace XER::Flux::Terminal {
namespace {

class StdTerminal final : public TerminalInterface {
public:
	void Write(OutputStream stream, std::string_view text) override {
		std::lock_guard<std::mutex> lock(mutex_);
		std::ostream& out = stream == OutputStream::kStdout ? std::cout : std::cerr;
		out << text;
	}

	void Flush(OutputStream stream) override {
		std::lock_guard<std::mutex> lock(mutex_);
		std::ostream& out = stream == OutputStream::kStdout ? std::cout : std::cerr;
		out.flush();
	}

private:
	std::mutex mutex_;
};

std::unique_ptr<TerminalInterface>& TerminalStorage() {
	static std::unique_ptr<TerminalInterface> terminal = std::make_unique<StdTerminal>();
	return terminal;
}

}  // namespace

TerminalInterface& GetDefaultTerminal() {
	return *TerminalStorage();
}

void SetDefaultTerminal(std::unique_ptr<TerminalInterface> terminal) {
	TerminalStorage() = terminal ? std::move(terminal) : std::make_unique<StdTerminal>();
}

}  // namespace XER::Flux::Terminal
