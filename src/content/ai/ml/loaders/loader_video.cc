#include "loader_video.h"

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#if __has_include(<opencv2/videoio.hpp>)
#  include <opencv2/core.hpp>
#  include <opencv2/videoio.hpp>
#  include <opencv2/imgproc.hpp>
#  define LOADER_VIDEO_HAS_OPENCV 1
#endif

#include <cstring>
#include <stdexcept>

namespace XER::ML::Loaders {

Dataset LoaderVideo::Load(const std::string& path) {
#ifdef LOADER_VIDEO_HAS_OPENCV
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    cv::VideoCapture cap(path);
    if (!cap.isOpened())
        throw std::runtime_error("VideoLoader: cannot open " + path);

    Dataset ds;
    cv::Mat frame, fframe;
    while (cap.read(frame)) {
        frame.convertTo(fframe, CV_32F, 1.0 / 255.0);
        const size_t total = fframe.total() * static_cast<size_t>(fframe.channels());
        std::vector<float> row(total);
        std::memcpy(row.data(), fframe.ptr<float>(), total * sizeof(float));
        ds.X.push_back(std::move(row));
    }
    StoreCachedDataset(Name(), path, ds);
    return ds;
#else
    (void)path;
    return Dataset{};
#endif
}

}  // namespace XER::ML::Loaders
