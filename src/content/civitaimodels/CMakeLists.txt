# ---------------------------------------------------------------------------
# civitaimodels — model hub clients (Civitai + Hugging Face) and the streaming
# cache downloader. Links against the project's network, cache and async I/O
# stacks; TLS via OpenSSL (already PUBLIC on XER_Network).
# ---------------------------------------------------------------------------

file(GLOB CIVITAI_MODEL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(XER_CivitaiModels STATIC ${CIVITAI_MODEL_SOURCES})

target_include_directories(XER_CivitaiModels
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_compile_features(XER_CivitaiModels PUBLIC cxx_std_23)

target_link_libraries(XER_CivitaiModels
    PUBLIC
        XER_Network
        XER_Cache
        XER_AsyncIO
        XER_Helper
        jsoncpp_lib
        OpenSSL::Crypto
)
