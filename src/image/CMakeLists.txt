# XER Image Subsystem CMake Configuration
#
# Image core (buffers/descriptors/color), format decoders (PNG/JPEG/WebP/
# GIF/EXR/HEIF/PSD/RAW/SVG/TIFF), effects filters/VFX, IO loaders and
# tools. Decoding is delegated to the FFmpeg/Skia engine bridges.

file(GLOB_RECURSE IMAGE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
file(GLOB_RECURSE IMAGE_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_Image STATIC ${IMAGE_SOURCES} ${IMAGE_HEADERS})

target_include_directories(XER_Image
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(XER_Image
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        range-v3::range-v3
        ZLIB::ZLIB
        XER_Flux_Core
        XER_FFmpeg
        XER_Helper
)

find_package(libxml2 CONFIG QUIET)
if(TARGET LibXml2::LibXml2)
    target_link_libraries(XER_Image PUBLIC LibXml2::LibXml2)
    target_compile_definitions(XER_Image PUBLIC XER_IMAGE_HAS_LIBXML2=1)
endif()

if(TARGET XER_BridgeSkia)
    target_link_libraries(XER_Image PUBLIC XER_BridgeSkia)
    target_compile_definitions(XER_Image PUBLIC XER_IMAGE_HAS_SKIA=1)
endif()

target_compile_features(XER_Image PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_Image PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Image PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
