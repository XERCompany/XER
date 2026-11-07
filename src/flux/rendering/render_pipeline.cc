#include "flux/rendering/render_pipeline.h"

#include <string>

#include "flux/terminal/terminal_output_renderer.h"

namespace XER::flux::rendering {

bool RenderPipeline::Present(const RenderContext& context, XER::flux::terminal::OutputStream stream) {
	const std::string plain = renderer_.RenderPlainText(context);
	const std::string ansi = renderer_.RenderAnsi(context);
	if (ansi == last_ansi_frame_) {
		return false;
	}
	XER::flux::terminal::Write(stream, ansi);
	last_plain_frame_ = plain;
	last_ansi_frame_ = ansi;
	++frame_count_;
	return true;
}

void RenderPipeline::Reset() {
	last_plain_frame_.clear();
	last_ansi_frame_.clear();
	frame_count_ = 0;
}

const std::string& RenderPipeline::last_plain_frame() const {
	return last_plain_frame_;
}

const std::string& RenderPipeline::last_ansi_frame() const {
	return last_ansi_frame_;
}

std::size_t RenderPipeline::frame_count() const {
	return frame_count_;
}

}  // namespace XER::flux::rendering
