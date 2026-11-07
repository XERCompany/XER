#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace XER::flux::terminal {

class TerminalEmulator {
public:
	void Feed(std::string_view text);
	void Reset();

	std::string PlainText() const;

private:
	std::vector<std::string> lines_;
	std::size_t cursor_row_ = 0;
	std::size_t cursor_column_ = 0;
};

std::string EmulatePlainText(std::string_view text);

}  // namespace XER::flux::terminal
