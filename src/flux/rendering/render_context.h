#pragma once

#include <string>

#include "flux/rendering/frame_buffer.h"
#include "flux/terminal/terminal_size_detector.h"
#include "flux/terminal/terminal_window.h"

namespace XER::Flux::Rendering {

class RenderContext {
public:
	explicit RenderContext(const XER::Flux::Terminal::TerminalSize& size = XER::Flux::Terminal::DefaultTerminalSize());
	static RenderContext FromWindow(const XER::Flux::Terminal::TerminalWindow& window);

	FrameBuffer& frame();
	const FrameBuffer& frame() const;

	const XER::Flux::Terminal::TerminalSize& size() const;
	void Resize(const XER::Flux::Terminal::TerminalSize& size);

	void Clear(char fill = ' ', const XER::Flux::Terminal::TerminalStyle& style = {});
	void SetTitle(std::string title);
	const std::string& title() const;

private:
	XER::Flux::Terminal::TerminalSize size_;
	FrameBuffer frame_;
	std::string title_;
};

}  // namespace XER::Flux::Rendering
