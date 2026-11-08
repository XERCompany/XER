#pragma once

#include <cstddef>

namespace XER::Flux::Terminal {

struct TerminalSize {
	std::size_t columns = 80;
	std::size_t rows = 25;
	bool interactive = false;
};

TerminalSize DefaultTerminalSize();
TerminalSize DetectTerminalSize();
TerminalSize ClampTerminalSize(const TerminalSize& size, std::size_t min_columns, std::size_t min_rows);

}  // namespace XER::Flux::Terminal
