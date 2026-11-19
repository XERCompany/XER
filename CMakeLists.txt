cmake_minimum_required(VERSION 3.21)

project(Editor)

# Set C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories for binaries and libraries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# Dependency provider selection.
#
#   * XER_USE_SYSTEM_DEPS=ON → cmake/deps/system_deps.cmake resolves deps
#     from OS packages: apt/brew on Linux/macOS (default ON there) and the
#     MSYS2 clang64 .pc/shim configs on Windows (opt-in via the
#     windows-clang-msys2 preset).
#   * Otherwise on Windows → the vcpkg toolchain below, unchanged. This
#     keeps the default/v8/v8-xer presets byte-identical: they never set
#     XER_USE_SYSTEM_DEPS, so the option stays OFF and the vcpkg branch
#     runs exactly as before.
if(WIN32)
    option(XER_USE_SYSTEM_DEPS
        "Resolve dependencies from MSYS2/system packages instead of vcpkg" OFF)
else()
    option(XER_USE_SYSTEM_DEPS
        "Resolve dependencies from OS packages (apt/brew) + partial vcpkg" ON)
endif()

if(XER_USE_SYSTEM_DEPS)
    include(cmake/deps/system_deps.cmake)
elseif(WIN32)
    # vcpkg integration (Windows default — covers every dependency).
    if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    endif()

    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    endif()

    # Set vcpkg triplet to x64-windows-release
    set(VCPKG_TARGET_TRIPLET "x64-windows-release" CACHE STRING "Vcpkg target triplet")
    set(VCPKG_MANIFEST_MODE OFF CACHE BOOL "Use preinstalled vcpkg packages")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime")
endif()

# Feature toggles (previously implicit -D cache vars; declared so they show
# up in CMake GUIs and presets can override them cleanly).
option(ENABLE_MEDIA "Enable media/content subsystems" ON)
option(ENABLE_V8 "Enable the V8 JavaScript runtime" OFF)
option(ENABLE_AI "Enable AI/ML content (ggml, ONNX, CUDA probes)" ON)
option(ENABLE_CUDA "Enable CUDA kernels/toolkit paths (.cu sources, CUDA bridges)" OFF)
option(ENABLE_CUDNN "Enable cuDNN paths" OFF)
option(XER_BUILD_LLVM "Build the bundled LLVM/Clang" OFF)
option(XER_BUILD_APP "Build the editor application" ON)
option(XER_BUILD_XBUILD "Build the xbuild configuration utility" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
file(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/main.h)

include(cmake/YaDll.cmake)

# Static archives get packed once into shared YA.*.dll wrappers
# (bin/, import libs in lib/) instead of ~170 MB into every exe.
option(XER_YA_DLLS "Pack static .a deps into shared YA.*.dll" ${MINGW})

# Vendored static assimp (built from source into third_party/assimp —
# MSYS2 ships only libassimp.dll.a). Prepend so CONFIG-mode
# find_package(assimp) picks it over the MSYS2 shared build.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/lib/cmake")
    list(PREPEND CMAKE_PREFIX_PATH
         "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp")
endif()
if(XER_YA_DLLS AND COMMAND xer_add_ya_dll
        AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/lib/libassimp.a")
    xer_add_ya_dll(YA_Assimp EXPORT_NAME "YA.Assimp"
        API_ARCHIVES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/lib/libassimp.a"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/assimp/lib/libzlibstatic.a")
endif()
if(XER_YA_DLLS AND COMMAND xer_add_ya_dll)
    # YA.YAML — libyaml-cpp.a packaged into the branded set;
    # consumers' yaml-cpp::yaml-cpp is retargeted to its implib.
    find_library(XER_YAMLCPP_STATIC NAMES libyaml-cpp.a
        PATHS "${CMAKE_PREFIX_PATH}" ${CMAKE_LIBRARY_PATH})
    if(XER_YAMLCPP_STATIC)
        xer_add_ya_dll(YA_YAML EXPORT_NAME "YA.YAML"
            API_ARCHIVES "${XER_YAMLCPP_STATIC}")
    endif()
    # YA.CoreInstant — reserved base module of the branded set.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ya")
    file(WRITE "${CMAKE_BINARY_DIR}/ya/YA_CoreInstant_src.cpp"
         "// YA.CoreInstant — reserved core-instantiation module.\n")
    xer_ya_module(YA_CoreInstant EXPORT_NAME YA.CoreInstant
        SOURCES "${CMAKE_BINARY_DIR}/ya/YA_CoreInstant_src.cpp")
endif()

find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(ZLIB REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(libuv CONFIG REQUIRED)
find_package(absl CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
# MSYS2/MinGW static linking: prefer the static jsoncpp archive and the
# static clang runtime so produced executables run without MSYS2 DLLs
# (libc++.dll, libunwind, libwinpthread, libjsoncpp-*.dll). Other
# heavy deps (skia, FFmpeg) stay dynamic — no .a is provided for them.
# XER_JsonCpp picks static when available and falls back to the shared
# import elsewhere (vcpkg, system packages).
if(TARGET jsoncpp_static)
    add_library(XER_JsonCpp INTERFACE)
    target_link_libraries(XER_JsonCpp INTERFACE jsoncpp_static)
    target_compile_definitions(XER_JsonCpp INTERFACE JSONCPP_STATIC_DEFINE)
else()
    add_library(XER_JsonCpp INTERFACE)
    target_link_libraries(XER_JsonCpp INTERFACE jsoncpp_lib)
endif()

# Static MSYS2 runtime for all executables (libc++/abi/unwind/
# winpthread + libxml2/iconv/z so statically-retargeted deps resolve).
# Kept narrow (not global -static) because some msys2 packages ship
# only .dll.a (skia); everything listed has a .a in clang64/lib.
# Set globally (before subdirectory targets are created) — a
# per-target list would keep missing tools like ztzmake.
if(MINGW AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # -lxml2 static only when libxml2 is NOT served by YA.LibXml2.dll —
    # the early archive would shadow the import lib and keep embedding
    # a static copy into every UI exe.
    # -licudt: static ICU's data archive — self-contained (no external
    # refs), so it is safe to list for every target; it is pulled only
    # when icuuc objects actually reference icudt78_dat.
    set(_xer_rt_static "-Wl,-Bstatic,-lc++,-lc++abi,-lunwind,-lwinpthread,-liconv,-lz,-lbz2,-llzma,-lbcrypt,-licudt")
    if(NOT TARGET YA_LibXml2)
        string(APPEND _xer_rt_static ",-lxml2")
    endif()
    string(APPEND _xer_rt_static ",-Bdynamic")
    # Static archives (OpenSSL/curl/…) need these system import libs —
    # they are always dynamic, so listing them for every target is safe.
    string(APPEND _xer_rt_static " -lws2_32 -lcrypt32 -lsecur32 -lncrypt -lwldap32")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " ${_xer_rt_static}")
    # YA.* modules get the same static runtime — an exe only stays
    # MSYS2-DLL-free when its YA dependencies are too. Third-party .a
    # archives are repointed per-target by xer_retarget_imported_to_static
    # (global -l injection would drag unused archives into every module).
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " ${_xer_rt_static}")
endif()

# Kept for call sites that already use it — flags now come from
# CMAKE_EXE_LINKER_FLAGS, so this is a no-op.
function(xer_link_msys2_static tgt)
endfunction()

# Several *_cef_deploy custom targets copy the same CEF Release/
# Resources into the shared bin/ dir — under -j they race on file
# locks ("Error copying directory: Permission denied"). Serialize
# them through a size-1 job pool.
set_property(GLOBAL PROPERTY JOB_POOLS xer_cef_deploy=1)

# Retargets msys2 SHARED IMPORTED targets matching a namespace regex
# onto their static .a archives (libfoo.dll.a -> libfoo.a) so
# executables do not drag MSYS2 DLLs around. Neither absl nor libxml2
# uses declspec(dllimport), so the switch is ABI-safe; transitive
# static deps (-liconv -lz for libxml2) ride in
# xer_link_msys2_static's -Bstatic list. Call in the same directory
# scope where the package was found (imported targets are
# directory-scoped).
function(xer_retarget_imported_to_static namespace_regex)
    if(NOT MINGW)
        return()
    endif()
    get_property(_xer_imported DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(_t IN LISTS _xer_imported)
        if(NOT _t MATCHES "${namespace_regex}")
            continue()
        endif()
        # Repoint INTERFACE_LINK_LIBRARIES first — interface-only
        # targets (PkgConfig::*, module shims) have no implib at all;
        # bare names ("curl"), "-lfoo" flags and full .dll.a paths
        # become absolute .a paths so pkg-config dep chains (curl's
        # nghttp2/idn2/psl, fftw3f, mupdf) resolve statically.
        get_property(_iface_libs TARGET ${_t}
                     PROPERTY INTERFACE_LINK_LIBRARIES)
        if(_iface_libs)
            set(_new_libs "")
            set(_changed OFF)
            foreach(_lib IN LISTS _iface_libs)
                set(_cand "")
                if(_lib MATCHES "\\.dll\\.a$")
                    string(REGEX REPLACE "\\.dll\\.a$" ".a"
                           _cand "${_lib}")
                elseif(_lib MATCHES "^-l(.+)$")
                    set(_cand
                        "${_xer_static_lib_dir}/lib${CMAKE_MATCH_1}.a")
                elseif(_lib MATCHES "^[A-Za-z0-9_.+-]+$" AND
                       NOT _lib MATCHES "${_xer_static_lib_skip}")
                    set(_cand "${_xer_static_lib_dir}/lib${_lib}.a")
                endif()
                if(_cand AND EXISTS "${_cand}")
                    list(APPEND _new_libs "${_cand}")
                    set(_changed ON)
                else()
                    list(APPEND _new_libs "${_lib}")
                endif()
            endforeach()
            if(_changed)
                set_property(TARGET ${_t}
                    PROPERTY INTERFACE_LINK_LIBRARIES "${_new_libs}")
            endif()
        endif()

        get_property(_implib TARGET ${_t}
                     PROPERTY IMPORTED_IMPLIB_RELEASE)
        if(NOT _implib)
            get_property(_implib TARGET ${_t}
                         PROPERTY IMPORTED_IMPLIB)
        endif()
        if(NOT _implib)
            # Some packages (libxml2) mark the .dll.a as LOCATION only.
            get_property(_implib TARGET ${_t}
                         PROPERTY IMPORTED_LOCATION_RELEASE)
        endif()
        if(NOT _implib)
            get_property(_implib TARGET ${_t}
                         PROPERTY IMPORTED_LOCATION)
        endif()
        if(NOT _implib)
            continue()
        endif()
        get_filename_component(_dir "${_implib}" DIRECTORY)
        get_filename_component(_fname "${_implib}" NAME)
        string(REGEX REPLACE "\\.dll\\.a$" ".a" _fname "${_fname}")
        set(_static_lib "${_dir}/${_fname}")
        if(EXISTS "${_static_lib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_LOCATION_RELEASE "${_static_lib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_LOCATION "${_static_lib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_IMPLIB_RELEASE "${_static_lib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_IMPLIB "${_static_lib}")
        endif()
    endforeach()
endfunction()

# Search dir + skip-list used by the interface-rewrite above.
get_filename_component(_xer_cc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(_xer_static_lib_dir "${_xer_cc_dir}/../lib"
                       ABSOLUTE)
set(_xer_static_lib_skip
    "^(ws2_32|wldap32|advapi32|user32|gdi32|gdiplus|ole32|oleaut32|uuid|shell32|comdlg32|winspool|dbghelp|psapi|userenv|winmm|iphlpapi|dnsapi|synchronization|rpcrt4|crypt32|secur32|bcrypt|ncrypt|kernel32|ntdll|msvcrt|msimg32|avicap32|dwrite|setupapi|version|cfgmgr32|avrt|powrprof|normaliz|comctl32|shlwapi|wlanapi|winhttp|bthprops|onecore|comsuppw|vulkan-1|mf|mfplat|mfreadwrite|mfuuid|dxva2|dxgi|d3d9|d3d11|d3d12|d3dcompiler|strmiids|evr|amstrmid|quartz)$")

# Repoints imported targets matching a namespace regex at the import
# library of a YA.* wrapper DLL (bin/YA.<name>.dll) so the archive code
# is shared instead of embedded per-exe. Call after find_package in the
# directory scope where the imported target lives.
function(xer_retarget_imported_to_ya_dll namespace_regex ya_target)
    if(NOT TARGET ${ya_target})
        return()
    endif()
    get_property(_oname TARGET ${ya_target} PROPERTY OUTPUT_NAME)
    set(_implib "${CMAKE_BINARY_DIR}/lib/lib${_oname}.dll.a")
    get_property(_xer_imported DIRECTORY PROPERTY IMPORTED_TARGETS)
    foreach(_t IN LISTS _xer_imported)
        if(_t MATCHES "${namespace_regex}")
            # IMPORTED_LOCATION doesn't evaluate genexes here — use the
            # literal implib path, plus the YA target itself in the
            # interface for the build-order edge.
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_LOCATION_RELEASE "${_implib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_LOCATION "${_implib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_IMPLIB_RELEASE "${_implib}")
            set_property(TARGET ${_t}
                PROPERTY IMPORTED_IMPLIB "${_implib}")
            set_property(TARGET ${_t} APPEND
                PROPERTY INTERFACE_LINK_LIBRARIES "${ya_target}")
        endif()
    endforeach()
endfunction()

xer_retarget_imported_to_static("^absl::")
# Catch-all: repoint every imported target whose import lib has a .a
# sibling (ICU/CURL/OpenSSL/sqlite3/zstd/brotli/FFTW3f/...). Libs that
# ship only .dll.a (skia, ggml, whisper, shaderc_shared, vulkan-1)
# keep their import lib automatically — the EXISTS check skips them.
xer_retarget_imported_to_static("^")

if(MINGW)
    # Defines required by the static variants' public headers — without
    # them the headers still mark symbols dllimport and the .a fails to
    # resolve (__imp_* references).
    add_compile_definitions(
        CURL_STATICLIB
        U_STATIC_IMPLEMENTATION
        LZMA_API_STATIC
        ARCHIVE_STATIC
        JSONCPP_STATIC_DEFINE)

    # find_package results in subdirectories get the same .dll.a -> .a
    # repointing. The macro executes in the calling directory scope, so
    # each subdir's DIRECTORY IMPORTED_TARGETS property is the right
    # one; find_dependency calls inside config files are covered too.
    macro(find_package)
        _find_package(${ARGV})
        xer_retarget_imported_to_static("^")
    endmacro()
endif()
find_package(re2 CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED) 
find_package(zstd CONFIG REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(unofficial-pthreadpool CONFIG REQUIRED)
find_package(unofficial-fxdiv CONFIG REQUIRED) 
find_package(CUDNN QUIET)
find_package(Eigen3 CONFIG REQUIRED)
find_package(range-v3 CONFIG REQUIRED)
find_package(mimalloc CONFIG REQUIRED)
find_package(ggml CONFIG REQUIRED)
find_package(folly CONFIG REQUIRED)
# Intel MKL: required where available (vcpkg on Windows, apt intel-mkl on
# Linux); absent on macOS where MKL has no arm64 build and on MinGW/MSYS2
# where no system package exists — all MKL code paths are gated on the
# ENGINE_HAS_MKL define / MKL::MKL target.
if(APPLE OR MINGW)
    find_package(MKL CONFIG QUIET)
else()
    find_package(MKL CONFIG REQUIRED)
endif()
if(TARGET MKL::MKL)
    # Intel MKL available project-wide (e.g. ENGINE_HAS_MKL gates the DFTI
    # backend in FFTEngine and other MKL-accelerated DSP paths).
    add_compile_definitions(ENGINE_HAS_MKL=1)
endif()
find_package(Boost CONFIG REQUIRED)
find_package(cereal CONFIG REQUIRED)
find_package(FFTW3f CONFIG REQUIRED)

# Static libcurl.a does not carry its own dep chain — the msys2
# CURL::libcurl target has no INTERFACE libs. Append the full static
# stack so consumers link it in one place.
if(MINGW AND TARGET CURL::libcurl)
    set(_xer_curl_deps "")
    foreach(_d IN ITEMS nghttp2 nghttp3 ngtcp2 ngtcp2_crypto_ossl
             ssl crypto zstd brotlidec brotlienc brotlicommon
             idn2 psl ssh2 unistring iconv z)
        set(_c "${_xer_static_lib_dir}/lib${_d}.a")
        if(EXISTS "${_c}")
            list(APPEND _xer_curl_deps "${_c}")
        endif()
    endforeach()
    set_property(TARGET CURL::libcurl
        APPEND PROPERTY INTERFACE_LINK_LIBRARIES ${_xer_curl_deps})
endif()
# Static OpenSSL: ssl needs crypto.
if(MINGW AND TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
    set_property(TARGET OpenSSL::SSL
        APPEND PROPERTY INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
endif()

# VST3 SDK (vcpkg copy when installed, otherwise fetched + built from source;
# see cmake/vst3sdk.cmake — portable across Windows/Linux/macOS).
set(XER_VST3SDK_USE_VCPKG ON)
include(cmake/vst3sdk.cmake)

add_subdirectory(src/flux)
add_subdirectory(src/cache)
add_subdirectory(src/wrapper/ffmpeg)
add_subdirectory(src/content/video)
add_subdirectory(src/sql)
add_subdirectory(src/node_graph)
add_subdirectory(src/async_io)
add_subdirectory(src/security_flash)
add_subdirectory(src/content/passwordgen)
add_subdirectory(src/ui)
add_subdirectory(src/audio)
add_subdirectory(src/content/audio)
add_subdirectory(src/content/virtualaudiocable)
add_subdirectory(src/content/clipboard)
add_subdirectory(src/content/hex)
add_subdirectory(src/content/network)
add_subdirectory(src/network)
add_subdirectory(src/tz)
add_subdirectory(src/cef)
add_subdirectory(src/content/nettest)
add_subdirectory(src/content/sim)
add_subdirectory(src/content/model3d)
add_subdirectory(src/content/modeldiff)
add_subdirectory(src/content/formatconv)
add_subdirectory(src/content/s3)
add_subdirectory(src/helper)
add_subdirectory(src/app)
if(WIN32)
    # lsui (Lua-scripted UI host) uses Win32 APIs directly — Windows only.
    add_subdirectory(src/lsui)
endif()

# Content subsystems — Chromium-style modular layer. Each directory owns a
# static library; optional/heavy backends (CUDA ops, libarchive, MuPDF,
# ggml/ONNX/OpenCV) self-gate inside their own CMakeLists.
add_subdirectory(src/error_handler)
add_subdirectory(src/runtime_safety)
add_subdirectory(src/content/platform)
add_subdirectory(src/content/process)
add_subdirectory(src/content/archive)
add_subdirectory(src/content/pdf)
add_subdirectory(src/content/civitaimodels)
add_subdirectory(src/content/openai)
add_subdirectory(src/content/device)
add_subdirectory(src/content/ai)

# Engine bridges — thin wrappers over GPU/media SDKs. angle/cuda/cudnn/opencv
# are availability stubs; skia links the real port when installed.
add_subdirectory(src/wrapper/angle)
add_subdirectory(src/wrapper/cuda)
add_subdirectory(src/wrapper/cudnn)
add_subdirectory(src/wrapper/opencv)
add_subdirectory(src/wrapper/skia)

# Media/content modules — image codecs/filters, video pipeline, ecosystem
# manifests and the pxer native-plugin host.
add_subdirectory(src/ecosystem)
add_subdirectory(src/image)
add_subdirectory(src/video)
add_subdirectory(src/media)
add_subdirectory(src/native)
add_subdirectory(src/lunca)
add_subdirectory(src/watch)

# XER language implementation (lexer/compiler/runtime) and the V8-gated
# JavaScript bridge layer.
add_subdirectory(src/xer)
add_subdirectory(src/javascript) 

# Engine-runtime support files that live at the src/ root: EngineParams,
# AppCommand, config helpers, Feature/Provider/ResourceGuard. They back the
# xer engine CLI (src/CMakeLists.txt) and are reused by runtime_safety/xer.
# resource_guard.{h,cc} is V8-dependent (v8::Isolate) — it lives with the
# other V8-gated engine sources in src/javascript/CMakeLists.txt.
add_library(XER_EngineSupport STATIC
    src/engine_params.cc
    src/app_command.cc
    src/audio_config_helper.cc
    src/feature.cc
    src/json_config_helper.cc
    src/provider.cc
)
target_include_directories(XER_EngineSupport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/..
)
target_link_libraries(XER_EngineSupport
    PUBLIC
        fmt::fmt
        XER_Flux_Core
        XER_Helper
)
target_compile_features(XER_EngineSupport PUBLIC cxx_std_23)
if(MSVC)
    target_compile_options(XER_EngineSupport PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(XER_EngineSupport PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Standalone VST3 scanner — loads plugin DLLs, so it runs as its own
# process away from the editor's UI/audio threads.
add_executable(EngineScanVST3 tools/EngineScanVST3/main.cc)
target_include_directories(EngineScanVST3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(EngineScanVST3 PUBLIC cxx_std_23)
target_link_libraries(EngineScanVST3 PRIVATE
    XER_AudioCore XER_Helper XER_AsyncIO)
set_target_properties(EngineScanVST3 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# XERVac.exe — Virtual Audio Cable service: named cross-process PCM
# cables, WASAPI bridges, and VST3/custom_fx chains in routes.
add_executable(XERVac tools/XERVirtualCable/main.cc)
target_include_directories(XERVac PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio
)
target_compile_features(XERVac PRIVATE cxx_std_23)
target_link_libraries(XERVac PRIVATE
    XER_ContentVAC XER_AudioCore XER_Helper XER_AsyncIO XER_JsonCpp)
set_target_properties(XERVac PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET XERVac POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/XERVirtualCable/vac.example.json"
        "${CMAKE_BINARY_DIR}/bin/vac.example.json")

# xer_engine_ui_render.exe — remote render worker. A UI app renders via
# Rendering::StreamRenderer onto a Cache::SharedMemoryChannel; this
# process replays the stream onto a real Direct2D surface and answers
# MeasureText requests so layout metrics stay exact across processes.
if(WIN32)
add_executable(xer_engine_ui_render tools/XERUIRender/main.cc)
set_target_properties(xer_engine_ui_render PROPERTIES
    OUTPUT_NAME "xer_engine_ui_render"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(xer_engine_ui_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)
target_compile_features(xer_engine_ui_render PRIVATE cxx_std_23)
target_link_libraries(xer_engine_ui_render PRIVATE
    XER_UI_Core XER_Cache XER_Helper)

# XERVacUI — second UI program (src/ui): virtual-cable control panel
# (cables/routes/FX/devices), JSON themes, UiEventBus hooks, remote
# render through xer_engine_ui_render.exe.
add_executable(XERVacUI tools/XERVacUI/main.cc)
set_target_properties(XERVacUI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(XERVacUI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)
target_compile_features(XERVacUI PRIVATE cxx_std_23)
target_link_libraries(XERVacUI PRIVATE
    XER_UI_Core XER_ContentVAC XER_Cache XER_Helper XER_JsonCpp)
endif()

if(WIN32)
# Windows service monitoring removable media for SecurityFlash tokens.
# The service itself only detects token presence; decryption stays with
# the interactive unlock flow that holds the passphrase.
add_executable(SecurityFlashService tools/SecurityFlashService/main.cc)
target_include_directories(SecurityFlashService PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(SecurityFlashService PRIVATE cxx_std_23)
target_link_libraries(SecurityFlashService PRIVATE
    XER_SecurityFlash
    XER_Helper
    wtsapi32
)
set_target_properties(SecurityFlashService PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# GUI front-end for SecurityFlashService: service control (SCM) plus
# removable-drive/token status, built on the XER UI widget set.
add_executable(ControllAccountsWindows tools/ControllAccountsWindows/main.cc)
target_include_directories(ControllAccountsWindows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(ControllAccountsWindows PRIVATE cxx_std_23)
target_link_libraries(ControllAccountsWindows PRIVATE
    XER_UI_Core
    user32
    gdi32
    shell32
)
set_target_properties(ControllAccountsWindows PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ServerAudio: TCP server that loads audio into cached ids, applies XER
# effects and saves the processed result (see tools/ServerAudio/main.cc).
add_executable(ServerAudio tools/ServerAudio/main.cc)
target_include_directories(ServerAudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(ServerAudio PRIVATE cxx_std_23)
target_link_libraries(ServerAudio PRIVATE
    XER_AudioCore
    XER_Network
    XER_Cache
    XER_Helper
)
if(WIN32)
    target_link_libraries(ServerAudio PRIVATE ole32)
endif()
set_target_properties(ServerAudio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# SecurityFlashHello: Windows Credential Provider (sign-in tile) that logs
# in with a SecurityFlash drive (flash-only or flash + PIN). Talks to
# SecurityFlashService over a SYSTEM-only pipe. COM loads it with the DLL's
# directory on the search path, so the vcpkg DLLs in bin/ resolve.
add_library(SecurityFlashHello SHARED
    tools/SecurityFlashHello/security_flash_hello.cc
    tools/SecurityFlashHello/SecurityFlashHello.def
)
target_include_directories(SecurityFlashHello PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(SecurityFlashHello PRIVATE cxx_std_23)
target_compile_definitions(SecurityFlashHello PRIVATE UNICODE _UNICODE)
target_link_libraries(SecurityFlashHello PRIVATE
    XER_SecurityFlash
    XER_Helper
    secur32
    ole32
    advapi32
)
set_target_properties(SecurityFlashHello PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif() # WIN32 — SecurityFlash service/GUI/credential provider

# ArchiveManager: web-UI archive manager (libarchive + XER http_server +
# UI password dialogs). The www/ assets are copied next to the exe.
add_executable(ArchiveManager tools/ArchiveManager/main.cc)
target_include_directories(ArchiveManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(ArchiveManager PRIVATE cxx_std_23)
target_link_libraries(ArchiveManager PRIVATE
    XER_Archive
    XER_Network
    XER_UI_Core
    XER_JsonCpp
)
if(WIN32)
    target_link_libraries(ArchiveManager PRIVATE ole32 oleaut32 user32 gdi32)
endif()
set_target_properties(ArchiveManager PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
add_custom_command(TARGET ArchiveManager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/ArchiveManager/www"
        "${CMAKE_BINARY_DIR}/bin/www")

# sd.exe — lightweight image generator CLI (stable-diffusion style).
# sd.exe generate.json — resolves weights locally or via Civitai, runs the
# sd_base pipeline and writes a PNG with webui-compatible metadata.
add_executable(sd
    tools/SdGenerate/main.cc
    tools/SdGenerate/sd_config.cc
    tools/SdGenerate/sd_lora.cc
    tools/SdGenerate/sd_model_resolver.cc
    tools/SdGenerate/sd_png_writer.cc
)
target_include_directories(sd PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(sd PRIVATE cxx_std_23)
target_link_libraries(sd PRIVATE
    XER_ContentAI
    XER_CivitaiModels
    XER_Image
    XER_JsonCpp
    ZLIB::ZLIB
)
# Capability probes inside XER_ContentAI (sd_gpu_dispatch) reference the
# CUDA/OpenCV bridge libs, which are configured after src/content/ai.
if(TARGET XER_BridgeCuda)
    target_link_libraries(sd PRIVATE XER_BridgeCuda)
endif()
if(TARGET XER_BridgeOpenCV)
    target_link_libraries(sd PRIVATE XER_BridgeOpenCV)
endif()
set_target_properties(sd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# HubCivitAIBrowser.exe — GUI Civitai model browser (search + filters +
# download) built on the src/ui widget framework.
add_executable(HubCivitAIBrowser
    tools/HubCivitAIBrowser/main.cc
    tools/HubCivitAIBrowser/hub_browser_widget.cc
    tools/HubCivitAIBrowser/hub_dialogs.cc
    tools/HubCivitAIBrowser/hub_preview_cache.cc
)
target_include_directories(HubCivitAIBrowser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/HubCivitAIBrowser
)
target_compile_features(HubCivitAIBrowser PRIVATE cxx_std_23)
target_link_libraries(HubCivitAIBrowser PRIVATE
    XER_UI_Core
    XER_CivitaiModels
    XER_Archive
)
set_target_properties(HubCivitAIBrowser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# audioconvert.exe — Audio format converter using XER audio codecs
add_executable(audioconvert tools/audioconvert/main.cc)
target_include_directories(audioconvert PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(audioconvert PRIVATE cxx_std_23)
target_link_libraries(audioconvert PRIVATE
    XER_AudioCore
    XER_Flux_Core
    XER_Helper
)
set_target_properties(audioconvert PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# audioapplyeffect.exe — Apply audio effects to input file and save to output
add_executable(audioapplyeffect tools/audioapplyeffect/main.cc)
target_include_directories(audioapplyeffect PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(audioapplyeffect PRIVATE cxx_std_23)
target_link_libraries(audioapplyeffect PRIVATE
    XER_AudioCore
    XER_Flux_Core
    XER_Helper
)
set_target_properties(audioapplyeffect PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# audioprobe.exe — Audio file information probe
add_executable(audioprobe tools/audioprobe/main.cc)
target_include_directories(audioprobe PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(audioprobe PRIVATE cxx_std_23)
target_link_libraries(audioprobe PRIVATE
    XER_AudioCore
    XER_Flux_Core
    XER_Helper
)
set_target_properties(audioprobe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# audioplayer.exe — Simple audio player using XER AudioPlayer
add_executable(audioplayer tools/audioplayer/main.cc)
target_include_directories(audioplayer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(audioplayer PRIVATE cxx_std_23)
target_link_libraries(audioplayer PRIVATE
    XER_App
    XER_Flux_Core
    XER_Helper
)
if(WIN32)
    target_link_libraries(audioplayer PRIVATE winmm)
endif()
set_target_properties(audioplayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# preview.exe — Image viewer/editor (GUI on Windows) + info tool
add_executable(preview tools/preview/main.cc)
target_include_directories(preview PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/preview
)
if(WIN32)
    target_sources(preview PRIVATE
        tools/preview/photo_browser.cc
        tools/preview/photo_viewer_widget.cc
        tools/preview/thumb_cache.cc
    )
endif()
target_compile_features(preview PRIVATE cxx_std_23)
target_link_libraries(preview PRIVATE
    XER_Image
    XER_Flux_Core
    XER_Helper
)
if(WIN32)
    target_link_libraries(preview PRIVATE XER_UI_Core)
endif()
set_target_properties(preview PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# addon/ — Node.js NAN native module (xer.node). Opt-in: needs Node.js
# headers + NAN fetched at configure time, so it stays OFF by default.
option(XER_BUILD_NODE_ADDON
    "Build the Node.js NAN addon (xer.node) in addon/" OFF)
if(XER_BUILD_NODE_ADDON)
    add_subdirectory(addon)
endif()

# VideoEditor (XER Studio Phase 1) — headless core lib; the exe lands in
# 1C/1D inside this same directory.
add_subdirectory(tools/tz)
add_subdirectory(tools/video_editor)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(demo)

add_executable(Editor
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(Editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(Editor PUBLIC cxx_std_23)

target_link_libraries(Editor
    PUBLIC
        $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
        unofficial::sqlite3::sqlite3
        absl::strings
        absl::synchronization
        absl::flat_hash_map
        absl::flat_hash_set
        unofficial::pthreadpool
        unofficial::fxdiv::fxdiv
        FFTW3::fftw3f
        fmt::fmt
        XER_JsonCpp
        re2::re2
        ZLIB::ZLIB
        range-v3::range-v3
        zstd::libzstd
        unofficial::brotli::brotlienc
        unofficial::brotli::brotlidec
        unofficial::brotli::brotlicommon
        ICU::uc
        ICU::i18n
        OpenSSL::SSL
        OpenSSL::Crypto
        XER_Helper
        XER_UI_Core
        XER_UI_Flux
        XER_Flux_Core
        XER_FFmpeg
        XER_AsyncIO
        XER_AudioCore
        XER_ContentAudio
        XER_App
)

if(WIN32)
    target_link_libraries(Editor PRIVATE
        Comctl32 dbghelp shlwapi winmm psapi advapi32 userenv shell32
        version ole32 ws2_32 crypt32 windowsapp wbemuuid)
endif()

target_include_directories(Editor PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(Editor PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(Editor PRIVATE ${FFMPEG_LIBRARIES})
# Compiler flags
if(MSVC)
    target_compile_options(Editor PRIVATE /W4 /WX- /FS)
else()
    target_compile_options(Editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Platform-specific flags
if(WIN32)
    target_compile_definitions(Editor PRIVATE _USE_MATH_DEFINES ENGINE_HAS_FFMPEG_BRIDGE ENGINE_HAS_FFMPEG_AVFILTER ENGINE_HAS_FFMPEG_AVDEVICE)
endif()
 

# Static MSYS2 runtime for all produced executables (libc++/abi/unwind/
# winpthread + static jsoncpp) — see xer_link_msys2_static above.
foreach(_t EngineScanVST3 XERVac xer_engine_ui_render XERVacUI
        SecurityFlashService ControllAccountsWindows ServerAudio
        ArchiveManager sd HubCivitAIBrowser audioconvert
        audioapplyeffect audioprobe audioplayer preview Editor)
    if(TARGET ${_t})
        xer_link_msys2_static(${_t})
    endif()
endforeach()
