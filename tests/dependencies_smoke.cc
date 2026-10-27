#include <absl/strings/str_cat.h>
#include <brotli/decode.h>
#include <brotli/encode.h>
#include <curl/curl.h>
#include <fmt/format.h>
#include <fxdiv.h>
#include <json/json.h>
#include <openssl/crypto.h>
#include <pthreadpool.h>
#include <re2/re2.h>
#include <sqlite3.h>
#include <uv.h>
#include <zlib.h>
#include <zstd.h>

#include <iostream>

#ifdef XER_TEST_FFTW
#include <fftw3.h>
#endif
#ifdef XER_TEST_OPENCV
#include <opencv2/core.hpp>
#endif
#ifdef XER_TEST_MEDIA
#define SDL_MAIN_HANDLED
#include <SDL.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

int main() {
    Json::Value value;
    value["status"] = absl::StrCat("vcpkg", "-", "ok");
    if (!RE2::FullMatch(value["status"].asString(), "vcpkg-ok")) return 1;
    sqlite3* database = nullptr;
    if (sqlite3_open(":memory:", &database) != SQLITE_OK) {
        sqlite3_close(database);
        return 2;
    }
    const int result = sqlite3_exec(database, "SELECT 1", nullptr, nullptr, nullptr);
    sqlite3_close(database);
    if (result != SQLITE_OK) return 3;
    auto* pool = pthreadpool_create(1);
    if (!pool) return 4;
    pthreadpool_destroy(pool);
    if (fxdiv_quotient_uint32_t(42, fxdiv_init_uint32_t(2)) != 21) return 5;
    if (!uv_version() || !zlibVersion() || !ZSTD_versionNumber() ||
        !BrotliEncoderVersion() || !BrotliDecoderVersion() ||
        !OpenSSL_version_num() || !curl_version()) return 6;
#ifdef XER_TEST_MEDIA
    SDL_version version{};
    SDL_GetVersion(&version);
    if (!version.major || !avcodec_version() || !avformat_version() ||
        !avutil_version() || !swresample_version() || !swscale_version()) return 7;
#endif
#ifdef XER_TEST_FFTW
    auto* fft_buffer = fftwf_alloc_real(16);
    if (!fft_buffer) return 8;
    fftwf_free(fft_buffer);
#endif
#ifdef XER_TEST_OPENCV
    if (cv::getVersionString().empty()) return 9;
#endif
    std::cout << fmt::format("{}\n", value["status"].asString());
    return 0;
}
