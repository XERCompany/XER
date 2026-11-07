#include "flux/terminal/terminal_window.h"

#include <algorithm>
#include <string>

#include "flux/terminal/terminal_output_renderer.h"

namespace XER::flux::terminal {

TerminalWindow::TerminalWindow(TerminalSize size)
	: size_(ClampTerminalSize(size, 20, 5)) {}

const TerminalSize& TerminalWindow::size() const {
	return size_;
}

void TerminalWindow::set_size(const TerminalSize& size) {
	size_ = ClampTerminalSize(size, 20, 5);
}

void TerminalWindow::RefreshSize() {
	const TerminalSize detected = DetectTerminalSize();
	if (detected.interactive || size_.columns == 0 || size_.rows == 0) {
		size_ = detected;
		return;
	}
	size_.interactive = detected.interactive;
}

std::string TerminalWindow::FitToWidth(std::string_view text) const {
	if (size_.columns == 0) {
		return std::string();
	}
	if (text.size() <= size_.columns) {
		return std::string(text);
	}
	if (size_.columns <= 3) {
		return std::string(text.substr(0, size_.columns));
	}
	std::string out(text.substr(0, size_.columns - 3));
	out += "...";
	return out;
}

std::string TerminalWindow::Centered(std::string_view text, char fill) const {
	std::string fitted = FitToWidth(text);
	if (fitted.size() >= size_.columns) {
		return fitted;
	}
	const std::size_t total_padding = size_.columns - fitted.size();
	const std::size_t left_padding = total_padding / 2;
	const std::size_t right_padding = total_padding - left_padding;
	return std::string(left_padding, fill) + fitted + std::string(right_padding, fill);
}

std::string TerminalWindow::MakeRule(char fill) const {
	return std::string(size_.columns, fill);
}

void TerminalWindow::WriteRule(OutputStream stream, char fill) const {
	WriteLine(stream, MakeRule(fill));
}

void TerminalWindow::WriteTitle(OutputStream stream, std::string_view title) const {
	WriteLine(stream, Centered(title));
}

}  // namespace XER::flux::terminal
