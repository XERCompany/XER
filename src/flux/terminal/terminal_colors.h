#pragma once

#include <string>

namespace XER::flux::terminal {

enum class TerminalColor {
	kDefault = -1,
	kBlack = 0,
	kRed = 1,
	kGreen = 2,
	kYellow = 3,
	kBlue = 4,
	kMagenta = 5,
	kCyan = 6,
	kWhite = 7,
	kBrightBlack = 8,
	kBrightRed = 9,
	kBrightGreen = 10,
	kBrightYellow = 11,
	kBrightBlue = 12,
	kBrightMagenta = 13,
	kBrightCyan = 14,
	kBrightWhite = 15,
};

inline bool IsDefaultColor(TerminalColor color) {
	return color == TerminalColor::kDefault;
}

inline std::string ForegroundSequence(TerminalColor color) {
	if (IsDefaultColor(color)) {
		return "\x1b[39m";
	}
	const int value = static_cast<int>(color);
	return std::string("\x1b[") + std::to_string(value < 8 ? 30 + value : 90 + (value - 8)) + "m";
}

inline std::string BackgroundSequence(TerminalColor color) {
	if (IsDefaultColor(color)) {
		return "\x1b[49m";
	}
	const int value = static_cast<int>(color);
	return std::string("\x1b[") + std::to_string(value < 8 ? 40 + value : 100 + (value - 8)) + "m";
}

inline std::string ResetColorSequence() {
	return "\x1b[39;49m";
}

}  // namespace XER::flux::terminal
