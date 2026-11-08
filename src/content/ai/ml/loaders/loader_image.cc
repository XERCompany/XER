#include "loader_image.h"

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#if __has_include(<opencv2/imgcodecs.hpp>)
#  include <opencv2/core.hpp>
#  include <opencv2/imgcodecs.hpp>
#  define LOADER_IMAGE_HAS_OPENCV 1
#endif

#include <cstring>
#include <stdexcept>

namespace XER::ML::Loaders {

Dataset LoaderImage::Load(const std::string& path) {
#ifdef LOADER_IMAGE_HAS_OPENCV
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (img.empty())
        throw std::runtime_error("ImageLoader: cannot read " + path);

    cv::Mat fimg;
    img.convertTo(fimg, CV_32F, 1.0 / 255.0);

    // Flatten HxWxC into a single feature row
    const size_t total = fimg.total() * static_cast<size_t>(fimg.channels());
    std::vector<float> row(total);
    std::memcpy(row.data(), fimg.ptr<float>(), total * sizeof(float));

    Dataset ds{{{std::move(row)}}, {}};
    StoreCachedDataset(Name(), path, ds);
    return ds;
#else
    (void)path;
    return Dataset{};
#endif
}

}  // namespace XER::ML::Loaders
