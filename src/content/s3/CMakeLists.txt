# XER S3 Subsystem CMake Configuration
# S3(-compatible) object storage client: AWS Signature V4 auth, transport via
# content/network (network::http::SyncRequest), ListObjectsV2 XML via pugixml.
# Used by the net.* NodeGraph nodes.

find_package(pugixml CONFIG REQUIRED)

add_library(XER_S3 STATIC
    s3_types.h
    s3_signer.h
    s3_signer.cc
    s3_client.h
    s3_client.cc
)

target_include_directories(XER_S3
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(XER_S3
    PUBLIC
        XER_Network
        OpenSSL::Crypto
        pugixml::pugixml
        XER_Hex
)

target_compile_features(XER_S3 PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(XER_S3 PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_S3 PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
