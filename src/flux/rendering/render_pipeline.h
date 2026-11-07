#pragma once

#include <cstddef>
#include <string>

#include "flux/rendering/renderer.h"
#include "flux/terminal/terminal_interface.h"

namespace XER::flux::rendering {

class RenderPipeline {
public:
	bool Present(const RenderContext& context, XER::flux::terminal::OutputStream stream = XER::flux::terminal::OutputStream::kStdout);
	void Reset();

	const std::string& last_plain_frame() const;
	const std::string& last_ansi_frame() const;
	std::size_t frame_count() const;

private:
	Renderer renderer_;
	std::string last_plain_frame_;
	std::string last_ansi_frame_;
	std::size_t frame_count_ = 0;
};

}  // namespace XER::flux::rendering
