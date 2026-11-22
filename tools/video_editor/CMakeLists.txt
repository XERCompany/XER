# VideoEditor core — headless timeline model/history/store/catalog/
# poster modules (Phase 1B). UI-free; consumed by the future
# video_editor exe (1C/1D) and XER_VideoEditor_Tests.
# CONFIGURE_DEPENDS so adding .cc files does not require a manual
# re-configure under Ninja.
# NOTE: argument order matters — <variable> must precede
# CONFIGURE_DEPENDS (flag-first parses but yields an empty list on
# CMake 4.4.x).
# Core sources: top level plus feature subdirs (playback/, vst/, lab/)
# added in Phase 2 — subdirs stay UI-free like the top level.
file(GLOB VIDEO_EDITOR_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/playback/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/vst/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/lab/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/ai/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/app_runtime/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/color/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/io/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/extensions/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/photo/*.cc"
)
# Exclude the exe's main() from the core lib glob (it must not be
# compiled into XER_VideoEditor_Core).
list(FILTER VIDEO_EDITOR_CORE_SOURCES EXCLUDE REGEX "/main\\.cc$")
# extensions/xsjo_jit.cc and extensions/ext_runtime_server.cc belong
# ONLY to the VE.ExtensionsRuntime worker exe (built below): the JIT
# host links LLVM, and the editor talks to it over worker_pipe — the
# core lib must stay LLVM-free.
list(FILTER VIDEO_EDITOR_CORE_SOURCES EXCLUDE REGEX
    "/extensions/(xsjo_jit|ext_runtime_server)\\.cc$")
# records_store.cc (recent-projects SQLite store) is kept OUT of the
# core glob so it can ship as its own branded module — YA.Records.dll —
# under XER_YA_DLLS. The static path appends it back below, so the TU
# builds exactly once either way.
list(FILTER VIDEO_EDITOR_CORE_SOURCES EXCLUDE REGEX
    "/records_store\\.cc$")

set(_ve_core_deps
    XER_Video          # video::Frame / FrameExporter (clip_poster)
    XER_ContentVideo   # XER::Content::Video::ProbeVideo (catalog)
    XER_FFmpeg         # Bridge::FFmpeg::VideoDecoder (clip_poster)
    XER_GpuVideo       # YA.CUDADecode/YA.CUDAEncode — runtime-probed GPU paths
    XER_FrameRender    # YA.FrameRender — frame->surface transport seam
    XER_SQL            # SQLiteDatabase (media_catalog)
    XER_Cache          # atomic export temp/commit helpers
    XER_NetClient      # app_super_net fetch/download (native_modules)
    XER_Tz             # .xexrj archives (extensions/ext_host)
    XER_Cef            # xer-ext:// scheme (stub when CEF absent)
    XER_Helper         # Path/env/jsoncpp helpers
    XER_JsonCpp        # project_store (.xerproj)
    XER_AudioCore      # VST3 host (plugin_wrappers) + DSP
)

# Photo workspace core (photo/: TiledDevice/Document/brush/history).
# Reuses src/image types — ImageBuffer for flatten, ImageLoader/
# ImageSaver for PNG I/O, ColorSpace for the working-space tag.
if(TARGET XER_Image)
    list(APPEND _ve_core_deps XER_Image)
endif()

# AI object detection (ai_tags.cc -> src/content/ai/find_object). The
# YA.AIFindObject module is delay-load-friendly: it probes its own
# backend DLLs before first call, so it reports unavailable rather
# than crashing when the ORT/OpenCV runtimes are absent.
if(TARGET XER_AIFindObject)
    list(APPEND _ve_core_deps XER_AIFindObject)
endif()

if(XER_YA_DLLS)
    # Branded YA.VideoEditor-* modules — XER_VideoEditor_Core stays as
    # an INTERFACE bundle so video_editor/tests link unchanged.
    # The editor's subsystems are mutually dependent (app_runtime boots
    # color/decoder/poster while catalog/ai call back into them), so
    # YA.VideoEditor-Core carries the whole module set; the specialized
    # names ship as reserved extension modules.
    # YA.Records is a REAL module (not a reserved shell): the recent-
    # projects records store lives here so persistence ships as its own
    # YA.* binary. It deliberately has no dependency on the editor core
    # (records_store.cc keeps its own duration formatter) — the core
    # links it instead, since smoke_checks.cc exercises the API.
    xer_ya_module(YA_Records EXPORT_NAME YA.Records
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/records_store.cc
        DEPS XER_SQL XER_Helper)
    xer_ya_module(YA_VideoEditor_Core EXPORT_NAME YA.VideoEditor-Core
        SOURCES ${VIDEO_EDITOR_CORE_SOURCES}
        DEPS ${_ve_core_deps} YA_Records)
    foreach(_ya_ve_ext IN ITEMS
            "YA_VideoEditor_OCIO|YA.VideoEditor-OCIO"
            "YA_VideoEditor_Engine|YA.VideoEditor-Engine"
            "YA_Extensions|YA.Extensions"
            "YA_ProjectStoreSQL_VE|YA.ProjectStoreSQL-VideoEditor"
            "YA_VideoEditor_FrameVideo|YA.VideoEditor-FrameVideo"
            "YA_VideoEditor_ToolsDesign|YA.VideoEditor-ToolsDesign")
        string(REGEX MATCH "^([^|]+)\\|(.*)$" _m "${_ya_ve_ext}")
        xer_ya_module(${CMAKE_MATCH_1} EXPORT_NAME ${CMAKE_MATCH_2}
            DEPS ${_ve_core_deps} YA_VideoEditor_Core)
    endforeach()
    # YA_Records rides the bundle too: the exe's UI sources call the
    # store API directly, so they need its import library — not just the
    # transitive runtime dep YA.VideoEditor-Core carries.
    xer_ya_bundle(XER_VideoEditor_Core
        MODULES YA_VideoEditor_Core YA_VideoEditor_Engine
                YA_VideoEditor_OCIO YA_VideoEditor_FrameVideo
                YA_VideoEditor_ToolsDesign YA_Extensions
                YA_ProjectStoreSQL_VE YA_Records
        DEPS ${_ve_core_deps}
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
    set(_xer_ve_scope INTERFACE)
else()
    add_library(XER_VideoEditor_Core STATIC
        ${VIDEO_EDITOR_CORE_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/records_store.cc)

    target_include_directories(XER_VideoEditor_Core
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    )
    set(_xer_ve_scope PUBLIC)
endif()

# App-runtime assets layer: real V8 (MSYS2 package via the V8 shim
# config) runs assets/runtime.js; yaml-cpp parses app.yaml/window.yaml.
# Both are QUIET/optional — without them the app boots with
# compiled-in defaults (the assets layer degrades gracefully).
find_package(V8 QUIET)
find_package(yaml-cpp QUIET)
xer_retarget_imported_to_ya_dll("yaml" YA_YAML)
find_package(OpenColorIO QUIET)
find_package(OpenImageIO QUIET)
find_package(librsvg QUIET)
if(TARGET librsvg::librsvg)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} librsvg::librsvg)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_RSVG=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoEditor_Core PRIVATE librsvg::librsvg)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE librsvg::librsvg)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_RSVG=1)
    endif()
endif()
if(TARGET OpenColorIO::OpenColorIO)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope}
        OpenColorIO::OpenColorIO)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_OCIO=1)
    if(XER_YA_DLLS)
        # OCIO consumers live in color/ -> YA.VideoEditor-Core.
        target_link_libraries(YA_VideoEditor_Core PRIVATE OpenColorIO::OpenColorIO)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE OpenColorIO::OpenColorIO)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_OCIO=1)
    endif()
    if(MINGW)
        # Delay-load target names for the LoadLibrary probe in
        # color/ocio_transform.cc — an absent runtime must degrade to
        # "unavailable" (the header contract), not terminate the process
        # when the delay-import first binds.
        xer_collect_delayload_dlls(_ve_ocio_rt OpenColorIO::OpenColorIO)
        if(_ve_ocio_rt)
            list(JOIN _ve_ocio_rt "|" _ve_ocio_rt_s)
            if(XER_YA_DLLS)
                target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
                    "XER_VE_OCIO_RUNTIME_DLLS=\"${_ve_ocio_rt_s}\"")
            else()
                target_compile_definitions(XER_VideoEditor_Core PUBLIC
                    "XER_VE_OCIO_RUNTIME_DLLS=\"${_ve_ocio_rt_s}\"")
            endif()
        endif()
    endif()
endif()
if(TARGET OpenImageIO::OpenImageIO)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope}
        OpenImageIO::OpenImageIO)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_OIIO=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoEditor_Core PRIVATE OpenImageIO::OpenImageIO)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE OpenImageIO::OpenImageIO)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_OIIO=1)
    endif()
    if(MINGW)
        # Delay-load target names for the LoadLibrary probe in
        # io/still_image.cc (the OIIO residual: dpx/hdr/pfm/ora/r3d/raw
        # decode + tiff/exr encode) — same "absent runtime degrades,
        # never kills" contract as the OCIO side.
        xer_collect_delayload_dlls(_ve_oiio_rt OpenImageIO::OpenImageIO)
        if(_ve_oiio_rt)
            list(JOIN _ve_oiio_rt "|" _ve_oiio_rt_s)
            if(XER_YA_DLLS)
                target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
                    "XER_VE_OIIO_RUNTIME_DLLS=\"${_ve_oiio_rt_s}\"")
            else()
                target_compile_definitions(XER_VideoEditor_Core PUBLIC
                    "XER_VE_OIIO_RUNTIME_DLLS=\"${_ve_oiio_rt_s}\"")
            endif()
        endif()
    endif()
    # OCIO/OIIO ship .dll.a only — delay-load them inside the module
    # so video_editor starts without the MSYS2 bin dir on PATH and
    # pulls them lazily on first color/image call. Delay-load only
    # defers the failure though: a DLL absent at first call still kills
    # the process (exit 127 under MSYS2 — the app_runtime ocio_transform/
    # image tests died exactly this way), so the toolchain runtime
    # closure is staged next to the binaries like src/ui does for libskia.
    if(MINGW)
        set(_ve_dl)
        foreach(_t OpenColorIO::OpenColorIO OpenImageIO::OpenImageIO)
            if(TARGET ${_t})
                xer_collect_delayload_dlls(_d ${_t})
                list(APPEND _ve_dl ${_d})
            endif()
        endforeach()
        if(_ve_dl)
            if(TARGET YA_VideoEditor_Core)
                xer_delayload(YA_VideoEditor_Core ${_ve_dl})
            endif()
            xer_delayload(XER_VideoEditor_Core ${_ve_dl})
            get_filename_component(_ve_toolchain_bin
                "${CMAKE_CXX_COMPILER}" DIRECTORY)
            xer_stage_runtime_dlls("${CMAKE_BINARY_DIR}/bin"
                "${_ve_toolchain_bin}" ${_ve_dl})
        endif()
    endif()
endif()
if(TARGET V8::V8)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} V8::V8)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_V8=1)
    if(XER_YA_DLLS)
        target_link_libraries(YA_VideoEditor_Core PRIVATE V8::V8)
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE V8::V8)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE XER_VE_HAS_V8=1)
    endif()
endif()
if(TARGET yaml-cpp::yaml-cpp)
    set(_ve_yaml_target yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(_ve_yaml_target yaml-cpp)
endif()
if(_ve_yaml_target)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} ${_ve_yaml_target})
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_YAMLCPP=1)
    if(XER_YA_DLLS)
        # app_runtime/color yaml loaders link inside the module itself.
        target_link_libraries(YA_VideoEditor_Core PRIVATE ${_ve_yaml_target})
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE ${_ve_yaml_target})
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
            XER_VE_HAS_YAMLCPP=1)
    endif()
endif()

if(WIN32)
    # WASAPI preview-audio sink (playback/wasapi_sink.cc).
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope} ole32)
endif()

# Skia powers the Text-clip rasterizer (text_style.cc) and the system
# font catalog (font_catalog.cc). Optional: without the port the same
# sources compile against the BitmapFont fallback that renders the
# legacy style subset, and FontCatalog::Available() reports false.
find_package(unofficial-skia CONFIG QUIET)
if(TARGET unofficial::skia::skia)
    if(XER_YA_DLLS)
        # objects: usage requirements (include dirs); module: the link.
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE
            unofficial::skia::skia)
        target_link_libraries(YA_VideoEditor_Core PRIVATE
            unofficial::skia::skia)
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
            XER_VE_HAS_SKIA=1)
    else()
        target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope}
            unofficial::skia::skia)
    endif()
    # Bundle/interface scope so dependents (the test suite) can tell
    # which text rasterizer RenderClipText will use — same pattern as
    # XER_VE_HAS_OCIO/XER_VE_HAS_RSVG above.
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        XER_VE_HAS_SKIA=1)
endif()

# ---------------------------------------------------------------------------
# Neural/AI backends (seam for upscale / object-detect / auto-caption
# features). Each XER_WITH_<name> option auto-defaults ON when the
# package is found and can be forced OFF on the configure line; the
# compiled-in set lands in nn_probe.cc as XER_VE_NN_<NAME>=1 defines
# (plus a *_VERSION string when the package config reports one) and is
# printed by the "nn_backends" smoke check.
#
# MSYS2 clang64 provides opencv / ggml / onnxruntime / onednn natively —
# their lib/cmake/ configs resolve via the toolchain prefix, no shims.
# What MSYS2 cannot provide stays runtime-probed or absent:
#   * MKL / OpenBLAS — lab/mkl_accel.cc LoadLibrary-probes mkl_rt /
#     libopenblas at run time (the MKL shim also picks up a manual
#     Intel oneAPI install); never import-linked here.
#   * CUDA / cuDNN — NVIDIA SDKs only (no MSYS2 package). The
#     CUDAToolkit module + cmake/deps/configs/CUDNN shim probe the
#     standard NVIDIA install dirs and report found/not-found for the
#     report matrix; src/wrapper/{cuda,cudnn} carry the real bridges.
# ---------------------------------------------------------------------------

find_package(OpenCV QUIET)
option(XER_WITH_OPENCV "VideoEditor: OpenCV backend (dnn / dnn_superres / imgproc)"
    ${OpenCV_FOUND})

# MSYS2/vcpkg export onnxruntime::onnxruntime; a couple of older
# packagings used bare "onnxruntime" or ONNX::onnxruntime.
find_package(onnxruntime QUIET)
set(_ve_ort_target "")
foreach(_ve_t onnxruntime::onnxruntime onnxruntime ONNX::onnxruntime)
    if(TARGET ${_ve_t} AND NOT _ve_ort_target)
        set(_ve_ort_target "${_ve_t}")
    endif()
endforeach()
if(_ve_ort_target)
    set(_ve_ort_default ON)
else()
    set(_ve_ort_default OFF)
endif()
option(XER_WITH_ONNX "VideoEditor: ONNX Runtime inference backend"
    ${_ve_ort_default})

find_package(DNNL QUIET)   # MSYS2 dnnl-config.cmake -> DNNL::dnnl
if(TARGET DNNL::dnnl)
    set(_ve_dnnl_default ON)
else()
    set(_ve_dnnl_default OFF)
endif()
option(XER_WITH_DNNL "VideoEditor: oneDNN primitives backend"
    ${_ve_dnnl_default})

find_package(ggml CONFIG QUIET)
if(TARGET ggml::ggml)
    set(_ve_ggml_default ON)
else()
    set(_ve_ggml_default OFF)
endif()
option(XER_WITH_GGML "VideoEditor: ggml tensor runtime backend"
    ${_ve_ggml_default})

# Report-only probes for the matrix — never linked into the editor.
find_package(CUDAToolkit QUIET)
find_package(CUDNN QUIET)   # cmake/deps/configs/CUDNN shim (NVIDIA dirs)

set(_ve_nn_libs "")   # link items for the core lib (bundle scope)
set(_ve_nn_defs "")   # XER_VE_NN_* compile definitions

if(XER_WITH_OPENCV)
    if(OpenCV_FOUND)
        list(APPEND _ve_nn_libs ${OpenCV_LIBS})
        set(_ve_v "${OpenCV_VERSION}")
        if(NOT _ve_v)
            set(_ve_v "unknown")
        endif()
        list(APPEND _ve_nn_defs XER_VE_NN_OPENCV=1
            "XER_VE_NN_OPENCV_VERSION=\"${_ve_v}\"")
    else()
        message(STATUS "video_editor: XER_WITH_OPENCV=ON but OpenCV not found")
    endif()
endif()
if(XER_WITH_ONNX)
    if(_ve_ort_target)
        list(APPEND _ve_nn_libs ${_ve_ort_target})
        set(_ve_v "${onnxruntime_VERSION}")
        if(NOT _ve_v)
            set(_ve_v "unknown")
        endif()
        list(APPEND _ve_nn_defs XER_VE_NN_ONNXRUNTIME=1
            "XER_VE_NN_ONNXRUNTIME_VERSION=\"${_ve_v}\"")
    else()
        message(STATUS "video_editor: XER_WITH_ONNX=ON but onnxruntime not found")
    endif()
endif()
if(XER_WITH_DNNL)
    if(TARGET DNNL::dnnl)
        list(APPEND _ve_nn_libs DNNL::dnnl)
        set(_ve_v "${DNNL_VERSION}")
        if(NOT _ve_v)
            set(_ve_v "unknown")
        endif()
        list(APPEND _ve_nn_defs XER_VE_NN_DNNL=1
            "XER_VE_NN_DNNL_VERSION=\"${_ve_v}\"")
    else()
        message(STATUS "video_editor: XER_WITH_DNNL=ON but oneDNN not found")
    endif()
endif()
if(XER_WITH_GGML)
    if(TARGET ggml::ggml)
        list(APPEND _ve_nn_libs ggml::ggml)
        set(_ve_v "${ggml_VERSION}")
        if(NOT _ve_v)
            set(_ve_v "unknown")
        endif()
        list(APPEND _ve_nn_defs XER_VE_NN_GGML=1
            "XER_VE_NN_GGML_VERSION=\"${_ve_v}\"")
    else()
        message(STATUS "video_editor: XER_WITH_GGML=ON but ggml not found")
    endif()
endif()

# Report-matrix defines for SDKs that are never import-linked here.
if(CUDAToolkit_FOUND)
    list(APPEND _ve_nn_defs XER_VE_NN_CUDA=1
        "XER_VE_NN_CUDA_VERSION=\"${CUDAToolkit_VERSION}\"")
endif()
if(CUDNN_FOUND)
    list(APPEND _ve_nn_defs XER_VE_NN_CUDNN=1)
endif()
if(TARGET MKL::MKL)
    list(APPEND _ve_nn_defs XER_VE_NN_MKL=1)
endif()

if(_ve_nn_defs)
    target_compile_definitions(XER_VideoEditor_Core ${_xer_ve_scope}
        ${_ve_nn_defs})
endif()
if(_ve_nn_libs)
    target_link_libraries(XER_VideoEditor_Core ${_xer_ve_scope}
        ${_ve_nn_libs})
    if(XER_YA_DLLS)
        # Same objects/module split as OCIO/OIIO above: nn_probe.cc is
        # compiled into the module's object set, so it needs the usage
        # requirements there; the module itself carries the link.
        target_link_libraries(YA_VideoEditor_Core_objs PRIVATE
            ${_ve_nn_libs})
        target_link_libraries(YA_VideoEditor_Core PRIVATE
            ${_ve_nn_libs})
        target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
            ${_ve_nn_defs})
    endif()
endif()

if(MINGW AND _ve_nn_libs)
    # MSYS2 ships shared-only builds (.dll.a implibs). Delay-load every
    # runtime DLL so video_editor.exe and the worker exes start without
    # the clang64 bin dir on PATH; the staged runtime closure next to
    # the binaries is what lets the delay-load actually resolve. Same
    # contract as the OCIO/OIIO block: an absent runtime must degrade,
    # never kill the process — nn_probe self-tests run first calls.
    xer_collect_delayload_dlls(_ve_nn_dlls ${_ve_nn_libs})
    xer_delayload(XER_VideoEditor_Core ${_ve_nn_dlls})
    if(TARGET YA_VideoEditor_Core)
        xer_delayload(YA_VideoEditor_Core ${_ve_nn_dlls})
    endif()
    # Per-backend delay-import name lists for nn_probe.cc — the smoke
    # LoadLibrary-probes these BEFORE the first delay-import call, so
    # a runtime whose closure is broken (e.g. a stale MSYS2 package
    # still wanting libabsl_*-<old>.dll) reports "unloadable" instead
    # of raising the uncatchable delay-load binder exception.
    set(_ve_nn_probe_defs "")
    set(_ve_nn_probe_groups "")
    if(XER_WITH_OPENCV AND OpenCV_FOUND)
        # Probe only the modules the editor core actually delay-imports
        # (nn_probe.cc exercises cv:: core+imgproc; lld emits
        # delay-import descriptors only for implibs whose symbols are
        # referenced — YA.VideoEditor-Core carries exactly these two).
        # OpenCV_LIBS spans ~55 MSYS2 modules that are neither
        # delay-imported nor staged into bin/ — probing any of those
        # reports a false "runtime unloadable" (libopencv_calib-500.dll,
        # the first OpenCV_LIBS entry, hit exactly this: Win32 error 126
        # on a perfectly healthy DLL that simply is not shipped).
        # The payload is comma-joined: a raw ;-list would splice into
        # _ve_nn_probe_groups as extra pseudo-groups.
        string(REPLACE ";" "," _ve_ocv_probe_targets
            "opencv_core;opencv_imgproc")
        list(APPEND _ve_nn_probe_groups
            "XER_VE_NN_OPENCV_DLLS|${_ve_ocv_probe_targets}")
    endif()
    if(XER_WITH_ONNX AND _ve_ort_target)
        list(APPEND _ve_nn_probe_groups
            "XER_VE_NN_ONNXRUNTIME_DLLS|${_ve_ort_target}")
    endif()
    if(XER_WITH_DNNL AND TARGET DNNL::dnnl)
        list(APPEND _ve_nn_probe_groups
            "XER_VE_NN_DNNL_DLLS|DNNL::dnnl")
    endif()
    if(XER_WITH_GGML AND TARGET ggml::ggml)
        list(APPEND _ve_nn_probe_groups
            "XER_VE_NN_GGML_DLLS|ggml::ggml")
    endif()
    foreach(_probe_group IN LISTS _ve_nn_probe_groups)
        string(REGEX MATCH "^([^|]+)\\|(.*)$" _pm "${_probe_group}")
        set(_probe_def "${CMAKE_MATCH_1}")
        set(_probe_targets "${CMAKE_MATCH_2}")
        # Comma-joined payload (see the OpenCV group above) back to a
        # list; a no-op for the single-target groups.
        string(REPLACE "," ";" _probe_targets "${_probe_targets}")
        set(_probe_ok "")
        foreach(_pt IN LISTS _probe_targets)
            if(TARGET ${_pt})
                list(APPEND _probe_ok ${_pt})
            endif()
        endforeach()
        if(_probe_ok)
            xer_collect_delayload_dlls(_probe_dlls ${_probe_ok})
            if(_probe_dlls)
                list(JOIN _probe_dlls "|" _probe_s)
                list(APPEND _ve_nn_probe_defs
                    "${_probe_def}=\"${_probe_s}\"")
            endif()
        endif()
    endforeach()
    if(_ve_nn_probe_defs)
        target_compile_definitions(XER_VideoEditor_Core
            ${_xer_ve_scope} ${_ve_nn_probe_defs})
        if(TARGET YA_VideoEditor_Core_objs)
            target_compile_definitions(YA_VideoEditor_Core_objs PRIVATE
                ${_ve_nn_probe_defs})
        endif()
    endif()
    get_filename_component(_ve_toolchain_bin "${CMAKE_CXX_COMPILER}"
        DIRECTORY)
    # Prefer Microsoft's official ORT binaries when vendored under
    # third_party/onnxruntime/win-x64 — must run BEFORE the seed walk
    # below so bin/onnxruntime.dll already exists (out_dir hits are
    # followed, never re-copied; xer_pe_deps resolves exe-dir first).
    xer_stage_vendored_ort("${CMAKE_BINARY_DIR}/bin")
    # ggml backend modules (ggml-cpu-*.dll, ggml-blas.dll, ...) are
    # dlopen'd by ggml_backend_load_all(), and ORT resolves its
    # onnxruntime_providers_shared.dll provider host at run time —
    # neither appears in an import table, so seed the staging walk
    # with them explicitly. The import-linked/delay-loaded NN DLLs
    # themselves (${_ve_nn_dlls}: opencv/onnxruntime/dnnl/ggml) are no
    # longer seeded — the xer_pe_deps POST_BUILD scan on video_editor
    # (below) computes the exact closure from the PE import tables of
    # the exe and its YA.*.dll siblings.
    file(GLOB _ve_nn_runtime "${_ve_toolchain_bin}/ggml-*.dll"
                             "${_ve_toolchain_bin}/onnxruntime*.dll")
    set(_ve_nn_extra "")
    foreach(_f IN LISTS _ve_nn_runtime)
        get_filename_component(_n "${_f}" NAME)
        list(APPEND _ve_nn_extra "${_n}")
    endforeach()
    xer_stage_runtime_dlls("${CMAKE_BINARY_DIR}/bin" "${_ve_toolchain_bin}"
        ${_ve_nn_extra})
    # MKL (manual oneAPI install, detected by the configs/MKL shim): the
    # mkl_rt*.dll import is delay-loaded via MKL::MKL's own link options;
    # stage the dispatcher + its mkl_core/mkl_def/mkl_avx* siblings from
    # the oneAPI bin dir so first call actually resolves.
    if(DEFINED MKL_BIN_DIR AND IS_DIRECTORY "${MKL_BIN_DIR}")
        file(GLOB _ve_mkl_rt "${MKL_BIN_DIR}/mkl_rt*.dll")
        set(_ve_mkl_names "")
        foreach(_f IN LISTS _ve_mkl_rt)
            get_filename_component(_n "${_f}" NAME)
            list(APPEND _ve_mkl_names "${_n}")
        endforeach()
        if(_ve_mkl_names)
            xer_stage_runtime_dlls("${CMAKE_BINARY_DIR}/bin"
                "${MKL_BIN_DIR}" ${_ve_mkl_names})
        endif()
    endif()
endif()

if(NOT XER_YA_DLLS)
    target_compile_features(XER_VideoEditor_Core PUBLIC cxx_std_23)
    if(MSVC)
        target_compile_options(XER_VideoEditor_Core PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_VideoEditor_Core PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
endif()

# video_editor.exe — Phase 1C headless CLI (--export/--info) plus the
# Phase 1D desktop workspace. UI sources live under ui/ so the core lib
# stays UI-free; they are exe-side only and Windows-gated like
# tools/preview.
add_executable(video_editor main.cc)
target_link_libraries(video_editor PRIVATE XER_VideoEditor_Core)
# First-party effect kernels are compiled in as builtins
# (builtin_fx.cc — registered by plugin_host.cc); only third-party
# DLLs live under <bin>/plugins, so the old plugins/ subdirectory is
# gone.
if(WIN32)
    # Recursive: Phase-2 panels/tools live in ui/ subdirs.
    file(GLOB_RECURSE VIDEO_EDITOR_UI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cc"
    )
    target_sources(video_editor PRIVATE ${VIDEO_EDITOR_UI_SOURCES})
    # ui2/ — the rebuilt shell (spec docs/wiki/architecture/
    # ve-ui-rebuild-spec.md). Parallel codebase: same exe, launched via
    # --ui2 / XER_VE_UI2=1 until the M4 swap-over.
    file(GLOB VIDEO_EDITOR_UI2_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/ui2/*.cc"
    )
    target_sources(video_editor PRIVATE ${VIDEO_EDITOR_UI2_SOURCES})
    target_link_libraries(video_editor PRIVATE XER_UI_Core)
    # CEF browser embedding — stub lib when the distribution is absent.
    if(TARGET XER_Cef)
        target_link_libraries(video_editor PRIVATE XER_Cef)
    endif()

    # No runtime-DLL deployment: all MSYS2 deps link statically, and
    # the remaining dll-only components (libskia, delay-loaded
    # ggml/whisper/opencv) are optional — the exe starts without them
    # and LoadLibrary-probes at first use. Assets and the CEF runtime
    # are still staged next to the exe.

    # App assets: app.yaml + runtime.js + window/window.yaml — the
    # declarative bootstrap the app_runtime layer executes via V8.
    add_custom_command(TARGET video_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:video_editor>/assets"
        COMMENT "Deploying app assets (runtime.js, yaml configs)"
        VERBATIM)
    # Always-run sibling of the POST_BUILD copy above: the hook
    # only fires on relink, so asset-only edits (icons, yaml,
    # runtime.js) would otherwise never reach the output dir. A
    # custom target builds on every build, deploying fresh assets
    # without a relink.
    add_custom_target(video_editor_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:video_editor>/assets"
        COMMENT "Deploying app assets (icons, yaml, runtime.js)"
        VERBATIM)
    # .xexrj extensions: every dir under assets/extensions_src/ that
    # carries a manifest.toml is packed by ztzmake into
    # <exe>/assets/extensions/<name>.xexrj — the same artifact a user
    # gets from running ztzmake.exe. Dirs without a manifest are
    # library sources (libdemo/), not extensions, and are skipped.
    #
    # .xejslib deps (tools/tz/xejslib.h): when XSJOBuildX was built
    # with real Clang support, libdemo/ is compiled into
    # <build>/xejslib/libdemo.xejslib and the demo pack consumes it
    # via ztzmake --dep — modules land as mobjective/lib_*.xsjo and
    # the lib's headers feed the demo's compile -I. Without Clang
    # the demo packs cleanly anyway (hello.cc's __has_include guard).
    set(_libdemo_xejslib "")
    if(TARGET XSJOBuildX AND XER_ZTZ_HAS_CLANG)
        set(_libdemo_src
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/extensions_src/libdemo")
        if(IS_DIRECTORY "${_libdemo_src}")
            set(_libdemo_xejslib
                "${CMAKE_BINARY_DIR}/xejslib/libdemo.xejslib")
            file(GLOB_RECURSE _libdemo_inputs CONFIGURE_DEPENDS
                 "${_libdemo_src}/*.cc" "${_libdemo_src}/*.cpp"
                 "${_libdemo_src}/*.h" "${_libdemo_src}/*.hpp")
            add_custom_command(
                OUTPUT "${_libdemo_xejslib}"
                COMMAND $<TARGET_FILE:XSJOBuildX>
                        "${_libdemo_src}"
                        -o "${_libdemo_xejslib}"
                        --name libdemo
                DEPENDS XSJOBuildX ${_libdemo_inputs}
                COMMENT "XSJOBuildX libdemo.xejslib"
                VERBATIM)
            add_custom_target(xejslib_libdemo
                DEPENDS "${_libdemo_xejslib}")
            add_dependencies(video_editor_assets xejslib_libdemo)
        endif()
    endif()
    if(TARGET ztzmake)
        file(GLOB XER_EXT_SRC_DIRS CONFIGURE_DEPENDS
             LIST_DIRECTORIES true
             "${CMAKE_CURRENT_SOURCE_DIR}/assets/extensions_src/*")
        foreach(ext_src IN LISTS XER_EXT_SRC_DIRS)
            if(IS_DIRECTORY "${ext_src}" AND
               EXISTS "${ext_src}/manifest.toml")
                get_filename_component(ext_name "${ext_src}" NAME)
                set(_ext_pack_args)
                if(ext_name STREQUAL "demo" AND _libdemo_xejslib)
                    list(APPEND _ext_pack_args
                         --dep "${_libdemo_xejslib}")
                endif()
                add_custom_command(TARGET video_editor_assets
                    POST_BUILD
                    COMMAND $<TARGET_FILE:ztzmake>
                            "${ext_src}"
                            -o "$<TARGET_FILE_DIR:video_editor>/assets/extensions/${ext_name}.xexrj"
                            ${_ext_pack_args}
                    COMMENT "Packing ${ext_name}.xexrj"
                    VERBATIM)
            endif()
        endforeach()
        add_dependencies(video_editor_assets ztzmake)
    endif()
    add_dependencies(video_editor video_editor_assets)

    # CEF runtime: libcef.dll + resources beside the exe — the
    # CefRuntime opens them from the executable directory. Manual
    # install via InstallCef.ps1 -> third_party/cef/cef_windows64.
    find_package(CEF QUIET)
    if(TARGET CEF::libcef)
        add_custom_target(video_editor_cef_deploy
            COMMAND ${CMAKE_COMMAND} -DSRC=${CEF_RELEASE_DIR} -DDST=$<TARGET_FILE_DIR:video_editor>
                       -P ${PROJECT_SOURCE_DIR}/cmake/CopyDirRetry.cmake
            COMMAND ${CMAKE_COMMAND} -DSRC=${CEF_RESOURCES_DIR} -DDST=$<TARGET_FILE_DIR:video_editor>
                       -P ${PROJECT_SOURCE_DIR}/cmake/CopyDirRetry.cmake
            COMMENT "Deploying CEF runtime (libcef.dll, *.pak, locales)"
            VERBATIM)
        set_property(TARGET video_editor_cef_deploy PROPERTY JOB_POOL xer_cef_deploy)
        add_dependencies(video_editor video_editor_cef_deploy)
        message(STATUS "CEF deploy target enabled (${CEF_ROOT})")
    endif()
endif()
target_compile_features(video_editor PRIVATE cxx_std_23)
set_target_properties(video_editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Precise runtime-DLL staging: xer_pe_deps walks the exe's PE import +
# delay-import tables at POST_BUILD, follows the YA.*.dll modules it
# sits next to (their delay-loads carry the opencv/ggml/onnxruntime/
# OCIO/OIIO runtimes), and copies only the toolchain DLLs actually
# referenced — replacing the hand-maintained seed lists above.
if(MINGW)
    get_filename_component(_ve_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    xer_stage_runtime_dlls(TARGET video_editor SEARCH "${_ve_tc_bin}")
endif()
if(MSVC)
    target_compile_options(video_editor PRIVATE /W4 /WX- /O2)
else()
    target_compile_options(video_editor PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()

# Dedicated named worker processes — thin mains over the shared
# JSON-lines worker loop (media_worker_server.cc + the role handlers
# in sync_media_server.cc / fx_script_server.cc). XERVideoLoader is
# the editor's out-of-process decode worker (MediaWorkerClient spawns
# it from the exe's own directory; `video_editor --media-worker` is
# the dev-build fallback); XERSyncMedia is the audio-sync engine
# (peak decode + correlation via sync_plan); XERFxScript is the
# sandboxed JS/V8 script-fx host (fx_list/fx_validate/fx_audio/
# fx_transition). Output goes next to video_editor.exe so the
# sibling-exe lookup in worker_pipe.cc works.
# Workers/ is NOT in the core glob: these files carry main().
add_executable(XERVideoLoader workers/video_loader_main.cc)
add_executable(XERSyncMedia workers/sync_media_main.cc)
add_executable(XERFxScript workers/fx_script_main.cc)

# VE.ExtensionsRuntime.exe — the out-of-process native-extension
# host: opens verified .xexrj archives, JIT-links mobjective/*.xsjo
# COFF objects with ORC LLJIT (extensions/xsjo_jit.cc) and serves
# ext_load/ext_call/ext_unload/ext_list/ext_events over the shared
# JSON-lines worker protocol (extensions/ext_runtime_server.cc —
# both files excluded from the core glob above). A crashing native
# extension kills this worker, never the editor.
add_executable(VE.ExtensionsRuntime
    workers/ext_runtime_main.cc
    extensions/ext_runtime_server.cc
    extensions/xsjo_jit.cc)
target_include_directories(VE.ExtensionsRuntime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
# The server's own TUs read TZ archives (XER_Tz) and encode JSON
# (XER_JsonCpp) directly — link them beside the core bundle.
target_link_libraries(VE.ExtensionsRuntime PRIVATE
    XER_VideoEditor_Core XER_Tz XER_JsonCpp)

# LLVM dev package (MSYS2 mingw-w64-clang-x86_64-llvm): headers under
# clang64/include, link the monolithic -lLLVM-23 import lib. Missing
# package → the exe still builds with XER_VE_HAS_LLVM=0 and answers
# ping/info/ext_list (ext_load of native payloads reports the JIT
# unavailable) so deployment never hard-fails.
find_package(LLVM CONFIG QUIET)
if(LLVM_FOUND)
    message(STATUS "VE.ExtensionsRuntime: LLVM ${LLVM_PACKAGE_VERSION} "
                   "(${LLVM_TOOLS_BINARY_DIR})")
    target_include_directories(VE.ExtensionsRuntime SYSTEM PRIVATE
        ${LLVM_INCLUDE_DIRS})
    # LLVM_DEFINITIONS is a space-separated string, not a list —
    # split it or it lands as one malformed compile flag.
    separate_arguments(_ve_llvm_defs NATIVE_COMMAND
        "${LLVM_DEFINITIONS}")
    target_compile_definitions(VE.ExtensionsRuntime PRIVATE
        XER_VE_HAS_LLVM=1 ${_ve_llvm_defs})
    if(TARGET LLVM)
        target_link_libraries(VE.ExtensionsRuntime PRIVATE LLVM)
    else()
        # Monolithic dylib without an imported target — link the
        # import lib by name (-lLLVM-<major>).
        target_link_directories(VE.ExtensionsRuntime PRIVATE
            ${LLVM_LIBRARY_DIRS})
        target_link_libraries(VE.ExtensionsRuntime PRIVATE
            LLVM-${LLVM_VERSION_MAJOR})
    endif()
    if(MINGW)
        # Stage the monolithic libLLVM-*.dll beside the exe (it is an
        # import-lib link, not static). Delay-load is NOT possible:
        # the dll carries __declspec(dllimport) data symbols
        # (llvm::ErrorList::ID) which lld cannot delay-bind, so the
        # import resolves eagerly at load — the staged copy is what
        # keeps the exe self-contained. XsjoJit::Available still
        # LoadLibrary-probes so a missing DLL reports cleanly.
        file(GLOB _ve_llvm_dlls "${LLVM_TOOLS_BINARY_DIR}/libLLVM*.dll")
        if(_ve_llvm_dlls)
            list(GET _ve_llvm_dlls 0 _ve_llvm_dll)
            get_filename_component(_ve_llvm_dll_name "${_ve_llvm_dll}" NAME)
            target_compile_definitions(VE.ExtensionsRuntime PRIVATE
                "XER_VE_LLVM_DLL=\"${_ve_llvm_dll_name}\"")
            add_custom_command(TARGET VE.ExtensionsRuntime POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_ve_llvm_dll}"
                    "$<TARGET_FILE_DIR:VE.ExtensionsRuntime>/${_ve_llvm_dll_name}"
                COMMENT "Deploying ${_ve_llvm_dll_name}"
                VERBATIM)
        endif()
    endif()
else()
    message(STATUS "VE.ExtensionsRuntime: LLVM not found — building "
                   "without JIT (XER_VE_HAS_LLVM=0)")
    target_compile_definitions(VE.ExtensionsRuntime PRIVATE
        XER_VE_HAS_LLVM=0)
endif()
# Same computed staging for the worker: it import-links libLLVM-*.dll
# (the copy_if_different above ships the DLL itself; the pe_deps walk
# additionally resolves libLLVM's own toolchain deps — libzstd/zlib/…).
if(MINGW)
    get_filename_component(_ve_tc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    xer_stage_runtime_dlls(TARGET VE.ExtensionsRuntime
        SEARCH "${_ve_tc_bin}")
endif()

foreach(_worker IN ITEMS XERVideoLoader XERSyncMedia XERFxScript
        VE.ExtensionsRuntime)
    target_link_libraries(${_worker} PRIVATE XER_VideoEditor_Core)
    target_compile_features(${_worker} PRIVATE cxx_std_23)
    set_target_properties(${_worker} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
        target_compile_options(${_worker} PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(${_worker} PRIVATE
            -Wall -Wextra -Wpedantic -O3)
    endif()
endforeach()
# Building the editor always produces its workers alongside — the
# smoke checks and the out-of-process paths (MediaPrep, audio sync,
# js fx, native extensions) expect the sibling exes (they degrade
# gracefully when one is absent).
add_dependencies(video_editor XERVideoLoader XERSyncMedia XERFxScript
    VE.ExtensionsRuntime)
