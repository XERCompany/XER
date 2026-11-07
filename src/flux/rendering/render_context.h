#pragma once

#include <string>

#include "flux/rendering/frame_buffer.h"
#include "flux/terminal/terminal_size_detector.h"
#include "flux/terminal/terminal_window.h"

namespace XER::flux::rendering {

class RenderContext {
public:
	explicit RenderContext(const XER::flux::terminal::TerminalSize& size = XER::flux::terminal::DefaultTerminalSize());
	static RenderContext FromWindow(const XER::flux::terminal::TerminalWindow& window);

	FrameBuffer& frame();
	const FrameBuffer& frame() const;

	const XER::flux::terminal::TerminalSize& size() const;
	void Resize(const XER::flux::terminal::TerminalSize& size);

	void Clear(char fill = ' ', const XER::flux::terminal::TerminalStyle& style = {});
	void SetTitle(std::string title);
	const std::string& title() const;

private:
	XER::flux::terminal::TerminalSize size_;
	FrameBuffer frame_;
	std::string title_;
};

}  // namespace XER::flux::rendering
