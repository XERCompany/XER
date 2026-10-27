if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows-release")
    message(FATAL_ERROR "The V8 source integration currently supports x64-windows-release only.")
endif()
if(NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "MultiThreadedDLL")
    message(FATAL_ERROR "V8 with x64-windows-release requires CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL.")
endif()
find_package(ICU REQUIRED COMPONENTS uc i18n data)
find_package(ZLIB REQUIRED)

set(XER_V8_VERSION "13.2.152.28")
set(XER_V8_REVISION "3d13fb751e6f2ecb9aeac06e65367704a12f5c19")
set(XER_DEPOT_TOOLS_REVISION "159aabc8235aeccb463493c999c182b6485a1381")
set(XER_V8_SOURCE_DIR "${XER_V8_DEPS_DIR}/v8" CACHE PATH "V8 source checkout")
set(XER_DEPOT_TOOLS_DIR "${XER_V8_DEPS_DIR}/depot_tools" CACHE PATH "Pinned depot_tools checkout")
set(XER_V8_BUILD_DIR "${XER_V8_SOURCE_DIR}/out/xer-release" CACHE PATH "V8 GN output directory")
set(XER_V8_BUILD_JOBS "4" CACHE STRING "Parallel V8 build jobs")
find_program(XER_V8_NINJA_EXECUTABLE NAMES ninja ninja.exe)
if(NOT XER_V8_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "XER_V8_BUILD_JOBS must be a positive integer.")
endif()
set(XER_V8_VCPKG_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(XER_V8_ICU_UC "${ICU_UC_LIBRARY_RELEASE}")
set(XER_V8_ICU_I18N "${ICU_I18N_LIBRARY_RELEASE}")
set(XER_V8_ICU_DATA "${ICU_DATA_LIBRARY_RELEASE}")
set(XER_V8_ZLIB "${ZLIB_LIBRARY_RELEASE}")
foreach(variable IN ITEMS XER_V8_VCPKG_PREFIX XER_V8_ICU_UC XER_V8_ICU_I18N
                          XER_V8_ICU_DATA XER_V8_ZLIB XER_V8_SOURCE_DIR
                          XER_DEPOT_TOOLS_DIR XER_V8_BUILD_DIR)
    if(NOT EXISTS "${${variable}}" AND variable MATCHES "^XER_V8_(ICU|ZLIB)")
        message(FATAL_ERROR "Missing release dependency ${variable}: ${${variable}}")
    endif()
    file(TO_CMAKE_PATH "${${variable}}" ${variable})
    if("${${variable}}" MATCHES "[\"$\n\r]")
        message(FATAL_ERROR "${variable} contains a character unsupported in generated GN paths.")
    endif()
endforeach()

set(XER_V8_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/v8-config")
file(MAKE_DIRECTORY "${XER_V8_CONFIG_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/v8/args.gn.in" "${XER_V8_CONFIG_DIR}/args.gn" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/v8/settings.cmake.in"
               "${XER_V8_CONFIG_DIR}/settings.cmake" @ONLY)
set(v8_command "${CMAKE_COMMAND}" "-DXER_V8_SETTINGS=${XER_V8_CONFIG_DIR}/settings.cmake")
if(XER_FETCH_V8)
    add_custom_target(xer_v8_fetch
        COMMAND ${v8_command} -DXER_V8_ACTION=fetch -P "${CMAKE_CURRENT_LIST_DIR}/v8/driver.cmake"
        USES_TERMINAL VERBATIM)
endif()
add_custom_target(xer_v8_configure
    COMMAND ${v8_command} -DXER_V8_ACTION=configure -P "${CMAKE_CURRENT_LIST_DIR}/v8/driver.cmake"
    USES_TERMINAL VERBATIM)
add_custom_target(xer_v8_build
    COMMAND ${v8_command} -DXER_V8_ACTION=build -P "${CMAKE_CURRENT_LIST_DIR}/v8/driver.cmake"
    BYPRODUCTS "${XER_V8_BUILD_DIR}/obj/v8_monolith.lib"
               "${XER_V8_BUILD_DIR}/gen/include/v8-gn.h"
    USES_TERMINAL VERBATIM)

add_library(XERV8Runtime INTERFACE)
add_library(V8::V8 ALIAS XERV8Runtime)
add_dependencies(XERV8Runtime xer_v8_build)
target_include_directories(XERV8Runtime INTERFACE
    "${XER_V8_SOURCE_DIR}/include" "${XER_V8_BUILD_DIR}/gen/include")
target_compile_definitions(XERV8Runtime INTERFACE V8_GN_HEADER)
target_link_libraries(XERV8Runtime INTERFACE
    "${XER_V8_BUILD_DIR}/obj/v8_monolith.lib"
    ICU::uc ICU::i18n ICU::data ZLIB::ZLIB
    dbghelp shlwapi winmm ws2_32 psapi advapi32 userenv shell32 version ole32)
set(V8_FOUND TRUE)
message(STATUS "V8 ${XER_V8_VERSION}: args.gn at ${XER_V8_CONFIG_DIR}/args.gn; fetch is an explicit target")
