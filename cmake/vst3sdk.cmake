# VST3 SDK integration.
#
# The vcpkg `vst3sdk` port installs no CMake package config and does not
# support static-CRT triplets (e.g. x64-windows-static), so CI cannot
# install it through vcpkg. When a vcpkg-installed copy is present it is
# used directly; otherwise the SDK is fetched from GitHub — pinned to the
# same tag the vcpkg port uses — and built from source via its own CMake
# project.
#
# Either way this defines the XER_VST3SDK interface target consumed by
# src/audio (include directories + libraries).

include(FetchContent)

set(XER_VST3SDK_VERSION "v3.8.1_build_84" CACHE STRING "VST3 SDK release tag")
set(XER_VST3SDK_URL_BASE "https://github.com/steinbergmedia" CACHE STRING
    "Base URL for the Steinberg VST3 SDK repositories")

# --- Path 1: SDK already installed by vcpkg -------------------------------
set(_xer_vst3sdk_vcpkg_root "")
if(XER_VST3SDK_USE_VCPKG AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(_xer_vst3sdk_vcpkg_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()

if(_xer_vst3sdk_vcpkg_root
        AND IS_DIRECTORY "${_xer_vst3sdk_vcpkg_root}/include/vst3sdk"
        AND IS_DIRECTORY "${_xer_vst3sdk_vcpkg_root}/lib/vst3sdk")
    set(_xer_vst3sdk_rel_libdir "${_xer_vst3sdk_vcpkg_root}/lib/vst3sdk")
    set(_xer_vst3sdk_dbg_libdir "${_xer_vst3sdk_vcpkg_root}/debug/lib/vst3sdk")

    add_library(XER_VST3SDK INTERFACE)
    target_include_directories(XER_VST3SDK INTERFACE
        "${_xer_vst3sdk_vcpkg_root}/include/vst3sdk")

    foreach(_xer_vst3sdk_lib IN ITEMS base pluginterfaces sdk sdk_common sdk_hosting)
        file(GLOB _xer_vst3sdk_rel
            "${_xer_vst3sdk_rel_libdir}/${_xer_vst3sdk_lib}.*"
            "${_xer_vst3sdk_rel_libdir}/lib${_xer_vst3sdk_lib}.*")
        list(FILTER _xer_vst3sdk_rel EXCLUDE REGEX "\\.(pdb|exp|manifest|dll|exe)$")
        if(NOT _xer_vst3sdk_rel)
            message(SEND_ERROR
                "vst3sdk: '${_xer_vst3sdk_lib}' not found in ${_xer_vst3sdk_rel_libdir}")
        endif()

        file(GLOB _xer_vst3sdk_dbg
            "${_xer_vst3sdk_dbg_libdir}/${_xer_vst3sdk_lib}.*"
            "${_xer_vst3sdk_dbg_libdir}/lib${_xer_vst3sdk_lib}.*")
        list(FILTER _xer_vst3sdk_dbg EXCLUDE REGEX "\\.(pdb|exp|manifest|dll|exe)$")
        list(GET _xer_vst3sdk_rel 0 _xer_vst3sdk_rel_first)
        if(_xer_vst3sdk_dbg)
            list(GET _xer_vst3sdk_dbg 0 _xer_vst3sdk_dbg_first)
            target_link_libraries(XER_VST3SDK INTERFACE
                "$<IF:$<CONFIG:Debug>,${_xer_vst3sdk_dbg_first},${_xer_vst3sdk_rel_first}>")
        else()
            target_link_libraries(XER_VST3SDK INTERFACE "${_xer_vst3sdk_rel_first}")
        endif()
    endforeach()

    message(STATUS "VST3 SDK: using vcpkg-installed copy (${_xer_vst3sdk_vcpkg_root})")

# --- Path 2: fetch and build the SDK from source --------------------------
else()
    # The SDK is split across repositories: the main vst3sdk repo contains
    # only the top-level CMakeLists and empty submodule mount points. The
    # vcpkg port assembles the same layout; doc/tutorials/vstgui4 are not
    # needed to build the libraries. SHA512 hashes are the same ones the
    # vcpkg port pins for this tag.
    set(XER_VST3SDK_SRC "${CMAKE_BINARY_DIR}/vst3sdk-src")
    set(XER_VST3SDK_BUILD "${CMAKE_BINARY_DIR}/vst3sdk-build")

    if(CMAKE_VERSION VERSION_LESS 3.25)
        message(FATAL_ERROR
            "Building the VST3 SDK from source requires CMake >= 3.25 "
            "(found ${CMAKE_VERSION}); install vst3sdk via vcpkg instead.")
    endif()

    FetchContent_Populate(vst3sdk
        URL "${XER_VST3SDK_URL_BASE}/vst3sdk/archive/refs/tags/${XER_VST3SDK_VERSION}.tar.gz"
        URL_HASH "SHA512=f55c03aca2fb994e48e6e8ff0de5d58e512a0a67556cf6bd86c0f9ae429732fca3408e41692b9302a88f7db5be11d3864558762868d47e2def0175b87b9e0937"
        SOURCE_DIR "${XER_VST3SDK_SRC}")

    FetchContent_Populate(vst3_base
        URL "${XER_VST3SDK_URL_BASE}/vst3_base/archive/refs/tags/${XER_VST3SDK_VERSION}.tar.gz"
        URL_HASH "SHA512=12de902576171233e382e0544cdfc8a651d6f640038569eb760ae1d3fe0fd43360952d1dcac209dd4224a9d6a669ce2ff41f9ba5279c35b8cf9622acd74d0857"
        SOURCE_DIR "${XER_VST3SDK_SRC}/base")

    FetchContent_Populate(vst3_cmake
        URL "${XER_VST3SDK_URL_BASE}/vst3_cmake/archive/refs/tags/${XER_VST3SDK_VERSION}.tar.gz"
        URL_HASH "SHA512=1e81e4a191220bd2750148338cdeedc119e8c20a221b56613ffa4d2491a1a27bd400fb6d0bb2a265ce3d280a1bf85dd18c56a5abacfebc7c268db70112edf8a3"
        SOURCE_DIR "${XER_VST3SDK_SRC}/cmake")

    FetchContent_Populate(vst3_pluginterfaces
        URL "${XER_VST3SDK_URL_BASE}/vst3_pluginterfaces/archive/refs/tags/${XER_VST3SDK_VERSION}.tar.gz"
        URL_HASH "SHA512=fd2218c3e949133dd51b4834c6680131a520587c4449fa50f9b86cb3de3c3091e34ff15e21ba0e995f078b43890e7aa239a1beeb753eba1ce6822f9b44875b3c"
        SOURCE_DIR "${XER_VST3SDK_SRC}/pluginterfaces")

    FetchContent_Populate(vst3_public_sdk
        URL "${XER_VST3SDK_URL_BASE}/vst3_public_sdk/archive/refs/tags/${XER_VST3SDK_VERSION}.tar.gz"
        URL_HASH "SHA512=d902a7b3f0d925e95d343df2378f4b701309d6460c970dfa064f0c2f24b0b4ca700ea15b3a4b63b3fc5f38cc5d6b2d55e5a3946fd10dd45960a86f948c868552"
        SOURCE_DIR "${XER_VST3SDK_SRC}/public.sdk")

    # Match the vcpkg port: no examples, no VSTGUI, no plugin link helper.
    set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF)
    set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF)
    set(SMTG_ENABLE_VSTGUI_SUPPORT OFF)
    set(SMTG_ENABLE_AUV2_BUILDS OFF)
    set(SMTG_CREATE_PLUGIN_LINK OFF)

    add_subdirectory("${XER_VST3SDK_SRC}" "${XER_VST3SDK_BUILD}")

    add_library(XER_VST3SDK INTERFACE)
    target_include_directories(XER_VST3SDK INTERFACE "${XER_VST3SDK_SRC}")
    target_link_libraries(XER_VST3SDK INTERFACE
        sdk_hosting sdk_common pluginterfaces base sdk)

    message(STATUS "VST3 SDK: building from source (${XER_VST3SDK_VERSION})")
endif()
