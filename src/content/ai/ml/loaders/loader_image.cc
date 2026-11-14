#include "loader_image.h"

#include "helper/string_utils.h"

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

#if __has_include(<opencv2/imgcodecs.hpp>)
#  include <opencv2/core.hpp>
#  include <opencv2/imgcodecs.hpp>
#  define LOADER_IMAGE_HAS_OPENCV 1
#endif

// tinyexr: HDR EXR loading (OpenCV does not decode EXR reliably).
#if __has_include(<tinyexr.h>)
#  include <tinyexr.h>
#  define LOADER_IMAGE_HAS_TINYEXR 1
#endif

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace XER::ML::Loaders {

#ifdef LOADER_IMAGE_HAS_TINYEXR
static bool IsExrPath(const std::string& path) {
    const std::string ext = XER::Helper::String::ToLowerAscii(
        std::filesystem::path(path).extension().string());
    return ext == ".exr";
}
#endif

Dataset LoaderImage::Load(const std::string& path) {
#ifdef LOADER_IMAGE_HAS_TINYEXR
    if (IsExrPath(path)) {
        Dataset cached;
        if (TryLoadCachedDataset(Name(), path, &cached)) {
            return cached;
        }

        float* pixels = nullptr;
        int width = 0, height = 0;
        const char* err = nullptr;
        const int status = LoadEXR(&pixels, &width, &height,
                                   path.c_str(), &err);
        if (status != TINYEXR_SUCCESS || !pixels || width <= 0 || height <= 0) {
            const std::string msg =
                err ? err : "tinyexr decode failed";
            if (err) FreeEXRErrorMessage(err);
            if (pixels) std::free(pixels);
            throw std::runtime_error("ImageLoader: " + msg + " (" + path + ")");
        }

        // tinyexr returns RGBA float32; flatten to a single feature row.
        const size_t total =
            static_cast<size_t>(width) * static_cast<size_t>(height) * 4U;
        Dataset ds{{{std::vector<float>(pixels, pixels + total)}}, {}};
        std::free(pixels);
        StoreCachedDataset(Name(), path, ds);
        return ds;
    }
#endif

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
