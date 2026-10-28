cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(xbuild VERSION 0.1.0 LANGUAGES C CXX)
    include(CTest)
endif()

find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(re2 CONFIG REQUIRED)
find_package(range-v3 CONFIG REQUIRED)
find_package(ZLIB CONFIG)
find_package(zstd CONFIG)
find_package(brotli CONFIG)
find_package(lzma CONFIG)
find_package(OpenSSL CONFIG)
find_package(CURL CONFIG)
find_package(SDL2 CONFIG)
find_package(SDL2_image CONFIG)
find_package(SDL2_mixer CONFIG)
find_package(SDL2_ttf CONFIG)
find_package(OpenCV CONFIG)
find_package(ffmpeg CONFIG)
find_package(freetype CONFIG)
find_package(harfbuzz CONFIG)
find_package(bzip2 CONFIG)
find_package(libxml2 CONFIG)
find_package(sqlite3 CONFIG)
find_package(pthreadpool CONFIG)
find_package(fxdiv CONFIG)
find_package(spdlog CONFIG)
find_package(fmt CONFIG)
find_package(gtest CONFIG)
find_package(gmock CONFIG)
find_package(boost-system CONFIG)
find_package(boost-filesystem CONFIG)
find_package(boost-thread CONFIG)
find_package(boost-program-options CONFIG)
find_package(protobuf CONFIG)
find_package(gRPC CONFIG)
find_package(zeromq CONFIG)
find_package(libarchive CONFIG)
find_package(libpng CONFIG)
find_package(jpeg CONFIG)
find_package(tiff CONFIG)
find_package(webp CONFIG)
find_package(gif CONFIG)

file(GLOB_RECURSE SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)

add_executable(xbuild ${SOURCES})
target_compile_features(xbuild PRIVATE cxx_std_23)
set_target_properties(xbuild PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(xbuild SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(XBUILD_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(xbuild PRIVATE ${XBUILD_LLVM_DEFINITIONS})
target_link_libraries(xbuild PRIVATE
    clangBasic LLVMSupport LLVMTargetParser LLVMAnalysis LLVMTransformUtils
    absl::strings absl::flat_hash_set absl::cleanup absl::synchronization
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    JsonCpp::JsonCpp re2::re2 range-v3::range-v3
    $<IF:$<TARGET_EXISTS:ZLIB::ZLIB>,ZLIB::ZLIB,>
    $<IF:$<TARGET_EXISTS:zstd::libzstd_shared>,zstd::libzstd_shared,>
    $<IF:$<TARGET_EXISTS:brotli::brotlienc>,brotli::brotlienc,>
    $<IF:$<TARGET_EXISTS:brotli::brotlidec>,brotli::brotlidec,>
    $<IF:$<TARGET_EXISTS:brotli::brotlicommon>,brotli::brotlicommon,>
    $<IF:$<TARGET_EXISTS:OpenSSL::SSL>,OpenSSL::SSL,>
    $<IF:$<TARGET_EXISTS:OpenSSL::Crypto>,OpenSSL::Crypto,>
    $<IF:$<TARGET_EXISTS:CURL::libcurl>,CURL::libcurl,>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,>
    $<IF:$<TARGET_EXISTS:SDL2_image::SDL2_image>,SDL2_image::SDL2_image,>
    $<IF:$<TARGET_EXISTS:SDL2_mixer::SDL2_mixer>,SDL2_mixer::SDL2_mixer,>
    $<IF:$<TARGET_EXISTS:SDL2_ttf::SDL2_ttf>,SDL2_ttf::SDL2_ttf,>
    $<IF:$<TARGET_EXISTS:OpenCV::opencv_core>,OpenCV::opencv_core,>
    $<IF:$<TARGET_EXISTS:OpenCV::opencv_imgproc>,OpenCV::opencv_imgproc,>
    $<IF:$<TARGET_EXISTS:OpenCV::opencv_highgui>,OpenCV::opencv_highgui,>
    $<IF:$<TARGET_EXISTS:freetype::freetype>,freetype::freetype,>
    $<IF:$<TARGET_EXISTS:harfbuzz::harfbuzz>,harfbuzz::harfbuzz,>
    $<IF:$<TARGET_EXISTS:bzip2::bzip2>,bzip2::bzip2,>
    $<IF:$<TARGET_EXISTS:libxml2::libxml2>,libxml2::libxml2,>
    $<IF:$<TARGET_EXISTS:sqlite3::sqlite3>,sqlite3::sqlite3,>
    $<IF:$<TARGET_EXISTS:pthreadpool::pthreadpool>,pthreadpool::pthreadpool,>
    $<IF:$<TARGET_EXISTS:fxdiv::fxdiv>,fxdiv::fxdiv,>
    $<IF:$<TARGET_EXISTS:spdlog::spdlog>,spdlog::spdlog,>
    $<IF:$<TARGET_EXISTS:fmt::fmt>,fmt::fmt,>
    $<IF:$<TARGET_EXISTS:GTest::gtest>,GTest::gtest,>
    $<IF:$<TARGET_EXISTS:GTest::gmock>,GTest::gmock,>
    $<IF:$<TARGET_EXISTS:Boost::system>,Boost::system,>
    $<IF:$<TARGET_EXISTS:Boost::filesystem>,Boost::filesystem,>
    $<IF:$<TARGET_EXISTS:Boost::thread>,Boost::thread,>
    $<IF:$<TARGET_EXISTS:Boost::program_options>,Boost::program_options,>
    $<IF:$<TARGET_EXISTS:protobuf::libprotobuf>,protobuf::libprotobuf,>
    $<IF:$<TARGET_EXISTS:gRPC::gRPC>,gRPC::gRPC,>
    $<IF:$<TARGET_EXISTS:ZeroMQ::libzmq>,ZeroMQ::libzmq,>
    $<IF:$<TARGET_EXISTS:libarchive::archive>,libarchive::archive,>
    $<IF:$<TARGET_EXISTS:libpng::libpng>,libpng::libpng,>
    $<IF:$<TARGET_EXISTS:JPEG::JPEG>,JPEG::JPEG,>
    $<IF:$<TARGET_EXISTS:TIFF::TIFF>,TIFF::TIFF,>
    $<IF:$<TARGET_EXISTS:libwebp::libwebp>,libwebp::libwebp,>
    $<IF:$<TARGET_EXISTS:gif::gif>,gif::gif,>)

add_definitions(${LLVM_DEFINITIONS_LIST})
 
llvm_map_components_to_libnames(LLVM_LIBS
    support
    core
    irreader
    option
    frontendopenmp
    target
    targetparser
) 
target_link_libraries(xbuild PRIVATE
    ${LLVM_LIBS}  
)

# Remove PDB dependency if it exists
if(TARGET LLVMDebugInfoPDB)
    get_target_property(_llvm_pdb_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    if(_llvm_pdb_libs)
        list(REMOVE_ITEM _llvm_pdb_libs "DIASDK::Diaguids")
        set_target_properties(LLVMDebugInfoPDB PROPERTIES INTERFACE_LINK_LIBRARIES "${_llvm_pdb_libs}")
    endif()
endif()
# LLVM's command-line classes are subclassed by cl::list in the executable.
# Match the installed LLVM's RTTI setting; retain exceptions for JSON errors.
if(NOT LLVM_ENABLE_RTTI)
    if(MSVC)
        target_compile_options(xbuild PRIVATE /GR-)
    else()
        target_compile_options(xbuild PRIVATE -fno-rtti)
    endif()
endif()
if(MSVC)
    target_compile_options(xbuild PRIVATE /W4 /permissive-)
else()
    target_compile_options(xbuild PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_test(NAME xbuild_cli COMMAND "${CMAKE_COMMAND}"
        "-DXBUILD_EXECUTABLE=$<TARGET_FILE:xbuild>"
        "-DXBUILD_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/cli-test"
        "-DXBUILD_EXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/project.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../tests/xbuild_cli_test.cmake")
endif()
