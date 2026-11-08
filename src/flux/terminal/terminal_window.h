#pragma once

#include <string>
#include <string_view>

#include "flux/terminal/terminal_interface.h"
#include "flux/terminal/terminal_size_detector.h"

namespace XER::Flux::Terminal {

class TerminalWindow {
public:
	explicit TerminalWindow(TerminalSize size = DefaultTerminalSize());

	const TerminalSize& size() const;
	void set_size(const TerminalSize& size);
	void RefreshSize();

	std::string FitToWidth(std::string_view text) const;
	std::string Centered(std::string_view text, char fill = ' ') const;
	std::string MakeRule(char fill = '-') const;

	void WriteRule(OutputStream stream, char fill = '-') const;
	void WriteTitle(OutputStream stream, std::string_view title) const;

private:
	TerminalSize size_;
};

}  // namespace XER::Flux::Terminal
