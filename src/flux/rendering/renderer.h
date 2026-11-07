#pragma once

#include <string>

#include "flux/rendering/render_context.h"

namespace XER::flux::rendering {

class Renderer {
public:
	std::string RenderPlainText(const RenderContext& context) const;
	std::string RenderAnsi(const RenderContext& context) const;
};

}  // namespace XER::flux::rendering
