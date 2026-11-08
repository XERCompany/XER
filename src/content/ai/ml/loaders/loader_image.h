#pragma once
#include "loader_base.h"

namespace XER::ML::Loaders {

/// Image loader using OpenCV (link with -lopencv_core -lopencv_imgcodecs).
/// Returns a single-row Dataset with pixels flattened and normalized to [0,1].
/// Returns empty Dataset when OpenCV is not available at compile time.
class LoaderImage : public LoaderBase {
public:
    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "ImageLoader"; }
};

}  // namespace XER::ML::Loaders
