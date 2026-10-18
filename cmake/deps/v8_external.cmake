if(NOT XER_FETCH_DEPENDENCIES OR NOT ENABLE_V8)
    return()
endif()

include(ExternalProject)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(XER_V8_VERSION "13.2.152.28" CACHE STRING "V8 source version")
set(XER_V8_BUILD_JOBS "4" CACHE STRING "Parallel jobs for the V8 source build")
set(_xer_v8_source "${XER_DEPS_DIR}/v8-src")
set(_xer_v8_icu_source "${_xer_v8_source}/third_party/icu/source")
set(_xer_depot_tools "${XER_DEPS_DIR}/depot_tools")
get_filename_component(_xer_python_dir "${Python3_EXECUTABLE}" DIRECTORY)
set(_xer_v8_path "${_xer_depot_tools};${_xer_python_dir};$ENV{PATH}")

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _xer_v8_processor)
if(_xer_v8_processor MATCHES "^(aarch64|arm64)$")
    set(_xer_v8_target_cpu "arm64")
elseif(_xer_v8_processor MATCHES "^(arm|armv7.*)$")
    set(_xer_v8_target_cpu "arm")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_xer_v8_target_cpu "x64")
else()
    set(_xer_v8_target_cpu "ia32")
endif()

ExternalProject_Add(XERDepotTools
    GIT_REPOSITORY https://chromium.googlesource.com/chromium/tools/depot_tools.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    SOURCE_DIR "${_xer_depot_tools}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND "")

if(WIN32)
    set(_xer_gclient "${_xer_depot_tools}/gclient.bat")
    set(_xer_gn "${_xer_depot_tools}/gn.bat")
    set(_xer_ninja "${_xer_depot_tools}/ninja.bat")
else()
    set(_xer_gclient "${_xer_depot_tools}/gclient")
    set(_xer_gn "${_xer_depot_tools}/gn")
    set(_xer_ninja "${_xer_depot_tools}/ninja")
endif()

set(_xer_v8_gclient_spec
    "solutions = [{ 'name': 'v8-src', 'url': 'https://chromium.googlesource.com/v8/v8.git@${XER_V8_VERSION}', 'managed': False }]")

ExternalProject_Add(XERV8SourceSync
    DEPENDS XERDepotTools
    PREFIX "${XER_DEPS_DIR}/v8-sync-external"
    SOURCE_DIR "${_xer_v8_source}"
    BINARY_DIR "${XER_DEPS_DIR}/v8-sync-build"
    DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -E make_directory "${_xer_v8_source}"
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
        "${CMAKE_COMMAND}" -E chdir "${XER_DEPS_DIR}"
        "${CMAKE_COMMAND}" -E env "PATH=${_xer_v8_path}" "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
        "${_xer_gclient}" config --spec "${_xer_v8_gclient_spec}"
    BUILD_COMMAND
        "${CMAKE_COMMAND}" -E chdir "${XER_DEPS_DIR}"
        "${CMAKE_COMMAND}" -E env "PATH=${_xer_v8_path}" "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
        "${_xer_gclient}" sync --no-history --shallow
    INSTALL_COMMAND "")

function(xer_add_v8_variant target_name variant_name build_dir is_debug)
    if(is_debug)
        set(_is_debug true)
    else()
        set(_is_debug false)
    endif()
    if(WIN32)
        set(_library "${build_dir}/obj/v8_monolith.lib")
    else()
        set(_library "${build_dir}/obj/libv8_monolith.a")
    endif()

    ExternalProject_Add(${target_name}
        DEPENDS XERV8SourceSync
        PREFIX "${XER_DEPS_DIR}/v8-${variant_name}-external"
        SOURCE_DIR "${_xer_v8_source}"
        BINARY_DIR "${build_dir}"
        DOWNLOAD_COMMAND ""
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=${_xer_v8_path}"
            "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
            "${_xer_gn}" gen "${build_dir}"
            "--args=is_debug=${_is_debug} is_component_build=false v8_monolithic=true v8_use_external_startup_data=false v8_enable_i18n_support=true icu_use_data_file=false v8_use_zlib=true symbol_level=0 target_cpu=\"${_xer_v8_target_cpu}\""
        BUILD_COMMAND "${CMAKE_COMMAND}" -E env
            "PATH=${_xer_v8_path}"
            "DEPOT_TOOLS_WIN_TOOLCHAIN=0"
            "${_xer_ninja}" -C "${build_dir}" -j "${XER_V8_BUILD_JOBS}" v8_monolith
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${_library}")
    set(${target_name}_LIBRARY "${_library}" PARENT_SCOPE)
    set(${target_name}_BUILD_DIR "${build_dir}" PARENT_SCOPE)
endfunction()

add_library(XERV8Runtime INTERFACE)
add_library(V8::V8 ALIAS XERV8Runtime)

if(WIN32)
    set(_xer_v8_debug_build "${XER_DEPS_DIR}/v8-build-debug")
    set(_xer_v8_release_build "${XER_DEPS_DIR}/v8-build-release")
    xer_add_v8_variant(XERV8Debug debug "${_xer_v8_debug_build}" TRUE)
    xer_add_v8_variant(XERV8Release release "${_xer_v8_release_build}" FALSE)
    add_custom_target(XERV8External)
    add_dependencies(XERV8External XERV8Debug XERV8Release)
    target_include_directories(XERV8Runtime INTERFACE
        "${_xer_v8_source}/include"
        "${_xer_v8_icu_source}/common"
        "${_xer_v8_icu_source}/i18n"
        "$<$<CONFIG:Debug>:${XERV8Debug_BUILD_DIR}/gen>"
        "$<$<NOT:$<CONFIG:Debug>>:${XERV8Release_BUILD_DIR}/gen>")
    target_link_libraries(XERV8Runtime INTERFACE
        "$<$<CONFIG:Debug>:${XERV8Debug_LIBRARY}>"
        "$<$<NOT:$<CONFIG:Debug>>:${XERV8Release_LIBRARY}>")
else()
    set(_xer_v8_release_build "${XER_DEPS_DIR}/v8-build")
    xer_add_v8_variant(XERV8Release release "${_xer_v8_release_build}" FALSE)
    add_custom_target(XERV8External)
    add_dependencies(XERV8External XERV8Release)
    target_include_directories(XERV8Runtime INTERFACE
        "${_xer_v8_source}/include"
        "${_xer_v8_icu_source}/common"
        "${_xer_v8_icu_source}/i18n"
        "${XERV8Release_BUILD_DIR}/gen")
    target_link_libraries(XERV8Runtime INTERFACE "${XERV8Release_LIBRARY}")
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(XERV8Runtime INTERFACE Threads::Threads ${CMAKE_DL_LIBS})
elseif(WIN32)
    target_link_libraries(XERV8Runtime INTERFACE
        dbghelp shlwapi winmm ws2_32 psapi advapi32 userenv shell32 version ole32)
endif()

set(V8_FOUND TRUE)
set(V8_INCLUDE_DIRS "${_xer_v8_source}/include")
set(XER_V8_SOURCE_ENABLED TRUE)