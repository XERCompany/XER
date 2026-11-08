#include "flux/rendering/renderer.h"

#include <string>
#include <vector>

#include "flux/terminal/terminal_styles.h"

namespace XER::Flux::Rendering {
namespace {

bool IsDefaultStyle(const XER::Flux::Terminal::TerminalStyle& style) {
	return style.foreground == XER::Flux::Terminal::TerminalColor::kDefault
		&& style.background == XER::Flux::Terminal::TerminalColor::kDefault
		&& !style.bold
		&& !style.dim
		&& !style.italic
		&& !style.underline
		&& !style.inverse;
}

bool StylesEqual(const XER::Flux::Terminal::TerminalStyle& left, const XER::Flux::Terminal::TerminalStyle& right) {
	return left.foreground == right.foreground
		&& left.background == right.background
		&& left.bold == right.bold
		&& left.dim == right.dim
		&& left.italic == right.italic
		&& left.underline == right.underline
		&& left.inverse == right.inverse;
}

}  // namespace

std::string Renderer::RenderPlainText(const RenderContext& context) const {
	const std::vector<std::string> lines = context.frame().PlainLines();
	std::string out;
	for (const std::string& line : lines) {
		out += line;
		out.push_back('\n');
	}
	return out;
}

std::string Renderer::RenderAnsi(const RenderContext& context) const {
	std::string out;
	for (std::size_t y = 0; y < context.frame().height(); ++y) {
		std::size_t last_non_blank = 0;
		bool has_visible_cells = false;
		for (std::size_t x = 0; x < context.frame().width(); ++x) {
			const FrameCell cell = context.frame().GetCell(x, y);
			if (cell.glyph != ' ' || !IsDefaultStyle(cell.style)) {
				last_non_blank = x;
				has_visible_cells = true;
			}
		}

		XER::Flux::Terminal::TerminalStyle current_style;
		bool style_active = false;
		const std::size_t width = has_visible_cells ? last_non_blank + 1 : 0;
		for (std::size_t x = 0; x < width; ++x) {
			const FrameCell cell = context.frame().GetCell(x, y);
			const bool cell_is_default = IsDefaultStyle(cell.style);
			if (!style_active || !StylesEqual(current_style, cell.style)) {
				if (style_active) {
					out += XER::Flux::Terminal::ResetStyleSequence();
				}
				if (!cell_is_default) {
					out += XER::Flux::Terminal::BeginStyleSequence(cell.style);
					style_active = true;
				} else {
					style_active = false;
				}
				current_style = cell.style;
			}
			out.push_back(cell.glyph);
		}
		if (style_active) {
			out += XER::Flux::Terminal::ResetStyleSequence();
		}
		out.push_back('\n');
	}
	return out;
}

}  // namespace XER::Flux::Rendering
