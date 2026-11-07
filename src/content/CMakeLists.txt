# XER Content Subsystem CMake Configuration

# Collect content sources
file(GLOB_RECURSE CONTENT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
)
if(APPLE)
    file(GLOB_RECURSE _xer_apple_mm "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
    list(APPEND CONTENT_SOURCES ${_xer_apple_mm})
endif()
list(FILTER CONTENT_SOURCES EXCLUDE REGEX "/(network|openai)/")

# Exclude CUDA ops when CUDA is disabled (no cuda_runtime.h available)
if(NOT ENABLE_CUDA)
    list(FILTER CONTENT_SOURCES EXCLUDE REGEX "/ai/ml/cuda_ops/")
endif()

# Exclude platform backends for other platforms
if(WIN32)
    list(FILTER CONTENT_SOURCES EXCLUDE REGEX "/platform/(linux|apple)/")
elseif(APPLE)
    list(FILTER CONTENT_SOURCES EXCLUDE REGEX "/platform/(linux|windows)/")
else()
    list(FILTER CONTENT_SOURCES EXCLUDE REGEX "/platform/(apple|windows)/")
endif()

file(GLOB_RECURSE CONTENT_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER CONTENT_HEADERS EXCLUDE REGEX "/(network|openai)/")
if(NOT ENABLE_CUDA)
    list(FILTER CONTENT_HEADERS EXCLUDE REGEX "/ai/ml/cuda_ops/")
endif()
if(WIN32)
    list(FILTER CONTENT_HEADERS EXCLUDE REGEX "/platform/(linux|apple)/")
elseif(APPLE)
    list(FILTER CONTENT_HEADERS EXCLUDE REGEX "/platform/(linux|windows)/")
else()
    list(FILTER CONTENT_HEADERS EXCLUDE REGEX "/platform/(apple|windows)/")
endif()

# Create content library
add_library(XER_Content STATIC ${CONTENT_SOURCES} ${CONTENT_HEADERS})

target_include_directories(XER_Content
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link with vcpkg libraries
target_link_libraries(XER_Content
    PUBLIC
        absl::strings
        absl::synchronization
        fmt::fmt
        jsoncpp_lib
        re2::re2
)

# Link with OpenSSL if available
if(TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    target_link_libraries(XER_Content PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Link with libcurl if available
if(TARGET CURL::libcurl)
    target_link_libraries(XER_Content PUBLIC CURL::libcurl)
endif()

# Link with zstd
target_link_libraries(XER_Content PUBLIC zstd::libzstd)

# AI/ML library backends (gated by ENABLE_AI in root CMakeLists.txt)
if(ENABLE_AI)
    target_link_libraries(XER_Content PUBLIC
        Eigen3::Eigen
        range-v3::range-v3
        Boost::boost
        cereal::cereal
    )
    target_link_libraries(XER_Content PRIVATE
        ggml::ggml
        Folly::folly
        mimalloc
    )
    # MKL as the unified BLAS/LAPACK backend
    if(TARGET MKL::MKL)
        target_link_libraries(XER_Content PRIVATE MKL::MKL)
    endif()
endif()

# C++ standard
target_compile_features(XER_Content PUBLIC cxx_std_23)

# Compiler flags
if(MSVC)
    target_compile_options(XER_Content PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_Content PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Platform-specific
if(WIN32)
    target_link_libraries(XER_Content PRIVATE ws2_32 crypt32)
endif()

# Add OpenAI subdirectory
# add_subdirectory(openai)

# Add network subdirectory
add_subdirectory(network)
target_link_libraries(XER_Content PUBLIC XER_OpenAI XER_Network)
