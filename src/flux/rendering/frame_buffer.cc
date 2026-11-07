#include "flux/rendering/frame_buffer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace XER::flux::rendering {
namespace {

std::string TrimRightSpaces(std::string line) {
	while (!line.empty() && line.back() == ' ') {
		line.pop_back();
	}
	return line;
}

}  // namespace

FrameBuffer::FrameBuffer(std::size_t width, std::size_t height) {
	Resize(width, height);
}

void FrameBuffer::Resize(std::size_t width, std::size_t height) {
	width_ = width;
	height_ = height;
	cells_.assign(width_ * height_, FrameCell{});
}

void FrameBuffer::Clear(char fill, const XER::flux::terminal::TerminalStyle& style) {
	for (FrameCell& cell : cells_) {
		cell.glyph = fill;
		cell.style = style;
	}
}

std::size_t FrameBuffer::width() const {
	return width_;
}

std::size_t FrameBuffer::height() const {
	return height_;
}

void FrameBuffer::SetCell(std::size_t x, std::size_t y, char glyph, const XER::flux::terminal::TerminalStyle& style) {
	if (!InBounds(x, y)) {
		return;
	}
	cells_[Index(x, y)] = FrameCell{glyph, style};
}

FrameCell FrameBuffer::GetCell(std::size_t x, std::size_t y) const {
	return InBounds(x, y) ? cells_[Index(x, y)] : FrameCell{};
}

void FrameBuffer::WriteText(std::size_t x, std::size_t y, std::string_view text, const XER::flux::terminal::TerminalStyle& style) {
	std::size_t cursor_x = x;
	std::size_t cursor_y = y;
	for (char ch : text) {
		if (ch == '\n') {
			++cursor_y;
			cursor_x = x;
			continue;
		}
		if (!InBounds(cursor_x, cursor_y)) {
			if (cursor_y >= height_) {
				break;
			}
			++cursor_x;
			continue;
		}
		SetCell(cursor_x, cursor_y, ch, style);
		++cursor_x;
	}
}

std::vector<std::string> FrameBuffer::PlainLines() const {
	std::vector<std::string> lines;
	lines.reserve(height_);
	for (std::size_t y = 0; y < height_; ++y) {
		std::string line;
		line.reserve(width_);
		for (std::size_t x = 0; x < width_; ++x) {
			line.push_back(GetCell(x, y).glyph);
		}
		lines.push_back(TrimRightSpaces(std::move(line)));
	}
	return lines;
}

std::size_t FrameBuffer::Index(std::size_t x, std::size_t y) const {
	return y * width_ + x;
}

bool FrameBuffer::InBounds(std::size_t x, std::size_t y) const {
	return x < width_ && y < height_;
}

}  // namespace XER::flux::rendering
