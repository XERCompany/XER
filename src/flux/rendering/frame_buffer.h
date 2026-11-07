#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "flux/terminal/terminal_styles.h"

namespace XER::flux::rendering {

struct FrameCell {
	char glyph = ' ';
	XER::flux::terminal::TerminalStyle style;
};

class FrameBuffer {
public:
	FrameBuffer(std::size_t width = 0, std::size_t height = 0);

	void Resize(std::size_t width, std::size_t height);
	void Clear(char fill = ' ', const XER::flux::terminal::TerminalStyle& style = {});

	std::size_t width() const;
	std::size_t height() const;

	void SetCell(std::size_t x, std::size_t y, char glyph, const XER::flux::terminal::TerminalStyle& style = {});
	FrameCell GetCell(std::size_t x, std::size_t y) const;
	void WriteText(std::size_t x, std::size_t y, std::string_view text, const XER::flux::terminal::TerminalStyle& style = {});

	std::vector<std::string> PlainLines() const;

private:
	std::size_t Index(std::size_t x, std::size_t y) const;
	bool InBounds(std::size_t x, std::size_t y) const;

	std::size_t width_ = 0;
	std::size_t height_ = 0;
	std::vector<FrameCell> cells_;
};

}  // namespace XER::flux::rendering
