add_executable(xer_dependencies_smoke dependencies_smoke.cc)
target_link_libraries(xer_dependencies_smoke PRIVATE
    $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    absl::strings fmt::fmt JsonCpp::JsonCpp re2::re2
    unofficial::sqlite3::sqlite3 ZLIB::ZLIB zstd::libzstd
    unofficial::brotli::brotlienc unofficial::brotli::brotlidec
    OpenSSL::SSL OpenSSL::Crypto CURL::libcurl
    unofficial::pthreadpool unofficial::fxdiv::fxdiv)
add_test(NAME dependencies_smoke COMMAND xer_dependencies_smoke)
add_test(NAME v8_prepare COMMAND "${CMAKE_COMMAND}"
    "-DXER_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/v8-prepare"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/v8_prepare_test.cmake")

if(ENABLE_MEDIA)
    target_link_libraries(xer_dependencies_smoke PRIVATE XER_FFmpeg SDL2::SDL2)
    target_compile_definitions(xer_dependencies_smoke PRIVATE XER_TEST_MEDIA=1)
endif()
if(TARGET FFTW3::fftw3f)
    target_link_libraries(xer_dependencies_smoke PRIVATE FFTW3::fftw3f)
    target_compile_definitions(xer_dependencies_smoke PRIVATE XER_TEST_FFTW=1)
endif()
if(ENABLE_OPENCV)
    target_link_libraries(xer_dependencies_smoke PRIVATE opencv_core)
    target_compile_definitions(xer_dependencies_smoke PRIVATE XER_TEST_OPENCV=1)
endif()

if(ENABLE_V8)
    add_executable(xer_v8_smoke v8_smoke.cc)
    target_link_libraries(xer_v8_smoke PRIVATE V8::V8)
    add_test(NAME v8_smoke COMMAND xer_v8_smoke)
endif()
