cmake_minimum_required(VERSION 3.21)
if(NOT DEFINED XER_TEST_DIR)
    message(FATAL_ERROR "Set XER_TEST_DIR to an isolated test directory.")
endif()
set(XER_V8_SOURCE_DIR "${XER_TEST_DIR}/source with spaces")
set(XER_V8_BUILD_DIR "${XER_TEST_DIR}/output with spaces")
set(XER_V8_CONFIG_DIR "${XER_TEST_DIR}/configuration")
file(MAKE_DIRECTORY "${XER_V8_SOURCE_DIR}/build/config/win" "${XER_V8_CONFIG_DIR}")
set(original [=[assert(is_win)
config("default_crt") {
  if (is_component_build) {
    configs = [ ":dynamic_crt" ]
  } else {
    configs = [ ":static_crt" ]
  }
}
config("release_crt") {
  if (is_component_build) {
    cflags = [ "/MD" ]
  } else {
    cflags = [ "/MT" ]
  }
}
]=])
set(windows_gn "${XER_V8_SOURCE_DIR}/build/config/win/BUILD.gn")
file(WRITE "${windows_gn}" "${original}")
file(WRITE "${XER_V8_CONFIG_DIR}/args.gn" "is_debug = false\nxer_use_dynamic_crt = true\n")
set(prepare "${CMAKE_CURRENT_LIST_DIR}/../cmake/v8/prepare.cmake")
include("${prepare}")
file(READ "${windows_gn}" patched)
foreach(config IN ITEMS default_crt release_crt)
    string(FIND "${patched}" "config(\"${config}\") {\n  if (is_component_build || xer_use_dynamic_crt)" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "${config} does not select the dynamic CRT.")
    endif()
endforeach()
if(NOT EXISTS "${XER_V8_SOURCE_DIR}/xer_vcpkg/zlib/google/BUILD.gn")
    message(FATAL_ERROR "Missing zlib compression adapter.")
endif()
file(READ "${XER_V8_BUILD_DIR}/args.gn" args)
if(NOT args STREQUAL "is_debug = false\nxer_use_dynamic_crt = true\n")
    message(FATAL_ERROR "args.gn did not reach the GN build directory.")
endif()
include("${prepare}")
file(READ "${windows_gn}" patched_twice)
if(NOT patched STREQUAL patched_twice)
    message(FATAL_ERROR "Preparing V8 a second time changed the CRT patch.")
endif()
file(WRITE "${windows_gn}" "unsupported upstream CRT layout\n")
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DXER_V8_SOURCE_DIR=${XER_V8_SOURCE_DIR}"
    "-DXER_V8_BUILD_DIR=${XER_V8_BUILD_DIR}"
    "-DXER_V8_CONFIG_DIR=${XER_V8_CONFIG_DIR}"
    -P "${prepare}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "Unsupported V8 Windows CRT configuration")
    message(FATAL_ERROR "An unsupported upstream CRT layout was not rejected: ${output}${error}")
endif()
file(READ "${windows_gn}" unchanged)
if(NOT unchanged STREQUAL "unsupported upstream CRT layout\n")
    message(FATAL_ERROR "The rejected upstream file was modified.")
endif()
message(STATUS "V8 preparation: CRT, adapters, paths with spaces, idempotence and rejection passed")
