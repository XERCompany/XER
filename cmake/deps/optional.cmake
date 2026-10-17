# ============================================================
# Optional / third-party dependencies + feature flags
# ============================================================

find_package(PkgConfig QUIET)
find_package(ICU COMPONENTS uc i18n QUIET)
find_package(SQLite3 QUIET)
if(ENABLE_V8)
    find_package(V8 QUIET)
endif()
if(NOT (XER_FETCH_DEPENDENCIES AND ENABLE_V8))
    find_package(libuv QUIET)
endif()
find_package(jsoncpp QUIET)
find_package(re2 QUIET)
find_package(range-v3 QUIET)
find_package(OpenCV QUIET)
find_package(HDF5 QUIET COMPONENTS C CXX)
find_package(Skia QUIET CONFIG)
if(ENABLE_AI OR ENABLE_V8)
    if(XER_FETCH_DEPENDENCIES)
        set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
        set(EIGEN_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        if(DEFINED BUILD_TESTING)
            set(_XER_BUILD_TESTING_WAS_DEFINED TRUE)
            set(_XER_BUILD_TESTING_SAVED "${BUILD_TESTING}")
        else()
            set(_XER_BUILD_TESTING_WAS_DEFINED FALSE)
        endif()
        set(BUILD_TESTING OFF)
        xer_fetch_cmake_dependency(xer_eigen https://gitlab.com/libeigen/eigen.git 3.4.0)
        if(_XER_BUILD_TESTING_WAS_DEFINED)
            set(BUILD_TESTING "${_XER_BUILD_TESTING_SAVED}")
        else()
            unset(BUILD_TESTING)
        endif()
    else()
        find_package(Eigen3 QUIET)
    endif()
else()
    find_package(Eigen3 QUIET)
endif()
find_package(OpenVINO QUIET)
find_package(Vulkan QUIET)
if(ENABLE_CUDA)
    find_package(CUDA QUIET)
    find_package(CUDAToolkit QUIET)
    find_package(CUTLASS QUIET)
endif()
find_package(fp16 QUIET)
if(NOT XER_FETCH_DEPENDENCIES)
    find_package(pthreadpool QUIET)
endif()
find_path(FFI_INCLUDE_DIR NAMES ffi.h)
find_library(FFI_LIBRARY NAMES ffi libffi)
find_path(FFTW3_INCLUDE_DIR NAMES fftw3.h)
find_library(FFTW3F_LIBRARY NAMES fftw3f)
find_path(LIBUSB_INCLUDE_DIR NAMES libusb.h PATH_SUFFIXES libusb-1.0)
find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0)
set(ENGINE_HAS_LIBUSB 0)
if(LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARY)
    set(ENGINE_HAS_LIBUSB 1)
endif()

set(ENGINE_HAS_FFI 0)
if(FFI_INCLUDE_DIR AND FFI_LIBRARY)
    set(ENGINE_HAS_FFI 1)
endif()

set(ENGINE_HAS_FFTW3F 0)
if(FFTW3_INCLUDE_DIR AND FFTW3F_LIBRARY)
    set(ENGINE_HAS_FFTW3F 1)
endif()

# ── HDF5 ────────────────────────────────────────────────────
set(ENGINE_HAS_HDF5 0)
if(HDF5_FOUND)
    set(ENGINE_HAS_HDF5 1)
elseif(PkgConfig_FOUND)
    pkg_check_modules(HDF5_PKG hdf5 QUIET)
    if(HDF5_PKG_FOUND)
        set(ENGINE_HAS_HDF5 1)
        set(HDF5_INCLUDE_DIRS ${HDF5_PKG_INCLUDE_DIRS})
        set(HDF5_LIBRARIES ${HDF5_PKG_LIBRARIES})
    endif()
endif()

# ── ICU ──────────────────────────────────────────────────────
set(ENGINE_HAS_ICU 0)
if(ICU_FOUND)
    set(ENGINE_HAS_ICU 1)
endif()

# ── SQLite3 ──────────────────────────────────────────────────
set(ENGINE_HAS_SQLITE3 0)
if(SQLite3_FOUND)
    set(ENGINE_HAS_SQLITE3 1)
endif()

# ── OpenCV ───────────────────────────────────────────────────
set(ENGINE_HAS_OPENCV_BRIDGE 0)
if(OpenCV_FOUND)
    set(ENGINE_HAS_OPENCV_BRIDGE 1)
endif()

# ── CUDA ─────────────────────────────────────────────────────
set(ENGINE_HAS_CUDA_BRIDGE 0)
if(ENABLE_CUDA AND CUDAToolkit_FOUND)
    set(ENGINE_HAS_CUDA_BRIDGE 1)
endif()

# ── cuDNN ────────────────────────────────────────────────────
if(ENABLE_CUDNN)
    find_path(CUDNN_INCLUDE_DIR NAMES cudnn.h)
    find_library(CUDNN_LIBRARY NAMES cudnn libcudnn)
endif()
set(ENGINE_HAS_CUDNN_BRIDGE 0)
if(ENABLE_CUDNN AND CUDNN_INCLUDE_DIR AND CUDNN_LIBRARY)
    set(ENGINE_HAS_CUDNN_BRIDGE 1)
endif()



# ── Skia ─────────────────────────────────────────────────────
set(ENGINE_HAS_SKIA_BRIDGE 0)
set(ENGINE_SKIA_INCLUDE_DIRS)
set(ENGINE_SKIA_LIBRARIES)
if(TARGET Skia::skia)
    set(ENGINE_HAS_SKIA_BRIDGE 1)
    list(APPEND ENGINE_SKIA_LIBRARIES Skia::skia)
elseif(PkgConfig_FOUND)
    pkg_check_modules(SKIA skia QUIET)
    if(SKIA_FOUND)
        set(ENGINE_HAS_SKIA_BRIDGE 1)
        list(APPEND ENGINE_SKIA_INCLUDE_DIRS ${SKIA_INCLUDE_DIRS})
        list(APPEND ENGINE_SKIA_LIBRARIES    ${SKIA_LIBRARIES})
    endif()
endif()

# ── FFmpeg ───────────────────────────────────────────────────
set(ENGINE_HAS_FFMPEG_BRIDGE 0)
set(ENGINE_HAS_FFMPEG_AVFILTER 0)
set(ENGINE_HAS_FFMPEG_AVDEVICE 0)
set(ENGINE_FFMPEG_INCLUDE_DIRS)
set(ENGINE_FFMPEG_LIBRARIES)

find_package(FFMPEG QUIET MODULE)
if(FFMPEG_FOUND AND FFMPEG_libavcodec_FOUND AND FFMPEG_libavformat_FOUND
        AND FFMPEG_libswscale_FOUND AND FFMPEG_libswresample_FOUND AND FFMPEG_libavutil_FOUND)
    set(ENGINE_HAS_FFMPEG_BRIDGE 1)
    set(ENGINE_FFMPEG_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIRS})
    set(ENGINE_FFMPEG_LIBRARIES ${FFMPEG_LIBRARIES})
    if(FFMPEG_libavfilter_FOUND)
        set(ENGINE_HAS_FFMPEG_AVFILTER 1)
    endif()
    if(FFMPEG_libavdevice_FOUND)
        set(ENGINE_HAS_FFMPEG_AVDEVICE 1)
    endif()
endif()

if(WIN32 AND ENGINE_HAS_FFMPEG_AVDEVICE)
    list(APPEND ENGINE_FFMPEG_LIBRARIES
        ole32
        oleaut32
        uuid
        strmiids
        user32
        gdi32
        shlwapi
        vfw32)
endif()

# ── ANGLE ────────────────────────────────────────────────────
find_path(ANGLE_INCLUDE_DIR  NAMES EGL/egl.h)
find_library(ANGLE_EGL_LIBRARY   NAMES EGL libEGL)
find_library(ANGLE_GLESV2_LIBRARY NAMES GLESv2 libGLESv2)
set(ENGINE_HAS_ANGLE_BRIDGE 0)
if(ANGLE_INCLUDE_DIR AND ANGLE_EGL_LIBRARY AND ANGLE_GLESV2_LIBRARY)
    set(ENGINE_HAS_ANGLE_BRIDGE 1)
endif()

# ── ZSTD ─────────────────────────────────────────────────────
if(NOT (XER_FETCH_DEPENDENCIES AND ENABLE_MEDIA))
    find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
    find_library(ZSTD_LIBRARY NAMES zstd libzstd)
endif()
set(ENGINE_HAS_ZSTD 0)
if(TARGET libzstd_static)
    set(ENGINE_HAS_ZSTD 1)
    set(ZSTD_INCLUDE_DIR "${xer_zstd_SOURCE_DIR}/lib")
    set(ZSTD_LIBRARY libzstd_static)
elseif(ZSTD_INCLUDE_DIR AND ZSTD_LIBRARY)
    set(ENGINE_HAS_ZSTD 1)
endif()

# ── Brotli ───────────────────────────────────────────────────
if(NOT (XER_FETCH_DEPENDENCIES AND ENABLE_MEDIA))
    find_path(BROTLI_INCLUDE_DIR NAMES brotli/encode.h)
    find_library(BROTLI_ENC_LIBRARY    NAMES brotlienc)
    find_library(BROTLI_DEC_LIBRARY    NAMES brotlidec)
    find_library(BROTLI_COMMON_LIBRARY NAMES brotlicommon)
endif()
set(ENGINE_HAS_BROTLI 0)
if(TARGET brotlienc AND TARGET brotlidec AND TARGET brotlicommon)
    set(ENGINE_HAS_BROTLI 1)
    set(BROTLI_INCLUDE_DIR "${xer_brotli_SOURCE_DIR}/c/include")
    set(BROTLI_ENC_LIBRARY brotlienc)
    set(BROTLI_DEC_LIBRARY brotlidec)
    set(BROTLI_COMMON_LIBRARY brotlicommon)
elseif(BROTLI_INCLUDE_DIR AND BROTLI_ENC_LIBRARY
        AND BROTLI_DEC_LIBRARY AND BROTLI_COMMON_LIBRARY)
    set(ENGINE_HAS_BROTLI 1)
endif()

# ── Qt6 ──────────────────────────────────────────────────────
set(ENGINE_QT6_COMPONENTS
    Concurrent Core DBus Gui
    LabsAnimation LabsFolderListModel LabsQmlModels LabsSettings
    LabsSharedImage LabsWavefrontMesh
    Network OpenGL OpenGLWidgets
    Pdf PdfQuick PdfWidgets Platform
    Positioning PositioningQuick PrintSupport
    Qml QmlCore QmlIntegration QmlLocalStorage
    QmlModels QmlWorkerScript QmlXmlListModel
    Quick QuickControls2 QuickControls2Impl
    QuickDialogs2 QuickDialogs2QuickImpl QuickDialogs2Utils
    QuickLayouts QuickTemplates2 QuickTest QuickWidgets
    Sql Test
    WebChannel WebView
    WebEngineCore WebEngineQuick WebEngineQuickDelegatesQml WebEngineWidgets
    Widgets Xml
)

find_package(Qt6 COMPONENTS ${ENGINE_QT6_COMPONENTS} QUIET)

set(ENGINE_HAS_QT6 0)
if(Qt6Core_FOUND)
    set(ENGINE_HAS_QT6 1)
endif()

# Individual Qt6 feature flags:
set(ENGINE_HAS_QT6_CORE       0)
set(ENGINE_HAS_QT6_GUI        0)
set(ENGINE_HAS_QT6_WIDGETS    0)
set(ENGINE_HAS_QT6_WEBCHANNEL 0)
set(ENGINE_HAS_QT6_WEBVIEW    0)
set(ENGINE_HAS_QT6_WEBENGINE  0)
set(ENGINE_HAS_QT6_QML        0)
set(ENGINE_HAS_QT6_QUICK      0)
if(Qt6Core_FOUND)
    set(ENGINE_HAS_QT6_CORE 1)
endif()
if(Qt6Gui_FOUND)
    set(ENGINE_HAS_QT6_GUI 1)
endif()
if(Qt6Widgets_FOUND)
    set(ENGINE_HAS_QT6_WIDGETS 1)
endif()
if(Qt6WebChannel_FOUND)
    set(ENGINE_HAS_QT6_WEBCHANNEL 1)
endif()
if(Qt6WebView_FOUND)
    set(ENGINE_HAS_QT6_WEBVIEW 1)
endif()
if(Qt6WebEngineCore_FOUND)
    set(ENGINE_HAS_QT6_WEBENGINE 1)
endif()
if(Qt6Qml_FOUND)
    set(ENGINE_HAS_QT6_QML 1)
endif()
if(Qt6Quick_FOUND)
    set(ENGINE_HAS_QT6_QUICK 1)
endif()

# Collect actually-available Qt6 targets
set(ENGINE_QT6_FOUND_TARGETS)
foreach(qt6_component IN LISTS ENGINE_QT6_COMPONENTS)
    if(TARGET Qt6::${qt6_component})
        list(APPEND ENGINE_QT6_FOUND_TARGETS Qt6::${qt6_component})
    endif()
endforeach()

if(ENGINE_QT6_FOUND_TARGETS)
    list(JOIN ENGINE_QT6_FOUND_TARGETS ", " _qt6_list_text)
    message(STATUS "Qt6 components available: ${_qt6_list_text}")
endif()
