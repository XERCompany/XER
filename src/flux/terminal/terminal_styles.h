#pragma once

#include <string>
#include <string_view>

#include "flux/terminal/terminal_colors.h"

namespace XER::flux::terminal {

struct TerminalStyle {
	TerminalColor foreground = TerminalColor::kDefault;
	TerminalColor background = TerminalColor::kDefault;
	bool bold = false;
	bool dim = false;
	bool italic = false;
	bool underline = false;
	bool inverse = false;
};

inline std::string BeginStyleSequence(const TerminalStyle& style) {
	std::string out;
	if (style.bold) {
		out += "\x1b[1m";
	}
	if (style.dim) {
		out += "\x1b[2m";
	}
	if (style.italic) {
		out += "\x1b[3m";
	}
	if (style.underline) {
		out += "\x1b[4m";
	}
	if (style.inverse) {
		out += "\x1b[7m";
	}
	if (!IsDefaultColor(style.foreground)) {
		out += ForegroundSequence(style.foreground);
	}
	if (!IsDefaultColor(style.background)) {
		out += BackgroundSequence(style.background);
	}
	return out;
}

inline std::string ResetStyleSequence() {
	return "\x1b[0m";
}

inline std::string ApplyStyle(std::string_view text, const TerminalStyle& style) {
	const std::string prefix = BeginStyleSequence(style);
	if (prefix.empty()) {
		return std::string(text);
	}
	std::string out(prefix);
	out.append(text.data(), text.size());
	out += ResetStyleSequence();
	return out;
}

}  // namespace XER::flux::terminal
