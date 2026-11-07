#include "flux/rendering/render_context.h"

namespace XER::flux::rendering {

RenderContext::RenderContext(const XER::flux::terminal::TerminalSize& size)
	: size_(XER::flux::terminal::ClampTerminalSize(size, 1, 1)),
	  frame_(size_.columns, size_.rows) {}

RenderContext RenderContext::FromWindow(const XER::flux::terminal::TerminalWindow& window) {
	return RenderContext(window.size());
}

FrameBuffer& RenderContext::frame() {
	return frame_;
}

const FrameBuffer& RenderContext::frame() const {
	return frame_;
}

const XER::flux::terminal::TerminalSize& RenderContext::size() const {
	return size_;
}

void RenderContext::Resize(const XER::flux::terminal::TerminalSize& size) {
	size_ = XER::flux::terminal::ClampTerminalSize(size, 1, 1);
	frame_.Resize(size_.columns, size_.rows);
}

void RenderContext::Clear(char fill, const XER::flux::terminal::TerminalStyle& style) {
	frame_.Clear(fill, style);
}

void RenderContext::SetTitle(std::string title) {
	title_ = std::move(title);
}

const std::string& RenderContext::title() const {
	return title_;
}

}  // namespace XER::flux::rendering
