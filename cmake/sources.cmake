# ============================================================
# Source file globs + filters
# ============================================================

file(GLOB_RECURSE ENGINE_ASYNC_IO_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/async_io/*.cc")
file(GLOB_RECURSE ENGINE_ASYNC_IO_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/async_io/*.h")

file(GLOB_RECURSE ENGINE_SRC_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
file(GLOB_RECURSE ENGINE_SRC_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

file(GLOB_RECURSE ENGINE_QT6_SUPPORT_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/qt6/*.cc")
file(GLOB_RECURSE ENGINE_QT6_SUPPORT_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/qt6/*.h")

file(GLOB_RECURSE ENGINE_QT6_MODULE_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/modules/qt6/*.cc")
file(GLOB_RECURSE ENGINE_QT6_MODULE_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/modules/qt6/*.h")

file(GLOB_RECURSE ENGINE_JAVASCRIPT_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/javascript/*.cc")
file(GLOB_RECURSE ENGINE_JAVASCRIPT_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/javascript/*.h")

file(GLOB_RECURSE ENGINE_WRAPPER_OPENCV_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/opencv/*.cc")
file(GLOB_RECURSE ENGINE_WRAPPER_OPENCV_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/opencv/*.h")

set(ENGINE_WRAPPER_CUDA_CC)
set(ENGINE_WRAPPER_CUDA_H)
if(ENABLE_CUDA)
    file(GLOB_RECURSE ENGINE_WRAPPER_CUDA_CC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/cuda/*.cc")
    file(GLOB_RECURSE ENGINE_WRAPPER_CUDA_H CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/cuda/*.h")
endif()

set(ENGINE_WRAPPER_CUDNN_CC)
set(ENGINE_WRAPPER_CUDNN_H)
if(ENABLE_CUDNN)
    file(GLOB_RECURSE ENGINE_WRAPPER_CUDNN_CC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/cudnn/*.cc")
    file(GLOB_RECURSE ENGINE_WRAPPER_CUDNN_H CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/cudnn/*.h")
endif()

file(GLOB_RECURSE ENGINE_WRAPPER_SKIA_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/skia/*.cc")
file(GLOB_RECURSE ENGINE_WRAPPER_SKIA_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/skia/*.h")

file(GLOB_RECURSE ENGINE_WRAPPER_FFMPEG_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/ffmpeg/*.cc")
file(GLOB_RECURSE ENGINE_WRAPPER_FFMPEG_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/ffmpeg/*.h")

file(GLOB_RECURSE ENGINE_WRAPPER_ANGLE_CC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/angle/*.cc")
file(GLOB_RECURSE ENGINE_WRAPPER_ANGLE_H CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/wrapper/angle/*.h")

# Qt6 combined
set(ENGINE_QT6_PROJECT_CC ${ENGINE_QT6_SUPPORT_CC} ${ENGINE_QT6_MODULE_CC})
set(ENGINE_QT6_PROJECT_H  ${ENGINE_QT6_SUPPORT_H}  ${ENGINE_QT6_MODULE_H})

set(ENGINE_SRC_CU)
if(ENABLE_CUDA)
    file(GLOB_RECURSE ENGINE_SRC_CU CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cu")
    if(NOT ENABLE_CUDNN)
        list(FILTER ENGINE_SRC_CU EXCLUDE REGEX "/src/content/ai/ml/cuda_ops/cuda_tensor_kernel\\.cu$")
    endif()
endif()

# Core sources — strip sub-trees that belong to separate static libs
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/wrapper/qt6/")
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/modules/qt6/")
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/async_io/")
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/javascript/")
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/wrapper/")
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/doctor/")
list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/v8/v8_runtime_checker\\.cc")
list(FILTER ENGINE_SRC_CU EXCLUDE REGEX "/src/wrapper/")
if(NOT ENABLE_CUDA)
    list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/content/ai/ml/cuda_ops/")
endif()
if(NOT ENABLE_CUDNN)
    list(FILTER ENGINE_ASYNC_IO_CC EXCLUDE REGEX "/src/async_io/dnn_backends/")
endif()
if(NOT ENABLE_AI)
    list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/content/ai/")
    list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/modules/module_ai\\.cc$")
    list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/modules/module_ai_ml_helpers\\.cc$")
    list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/modules/module_sd\\.cc$")
else()
    list(FILTER ENGINE_SRC_CC EXCLUDE REGEX "/src/modules/module_ai_disabled\\.cc$")
endif()
list(APPEND ENGINE_SRC_CC "${CMAKE_CURRENT_SOURCE_DIR}/src/xer/encode/sxer84321.c")

list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/wrapper/qt6/")
list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/modules/qt6/")
list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/async_io/")
list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/javascript/")
list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/wrapper/")
list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/doctor/")
list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/v8/v8_runtime_checker\\.h$")
if(NOT ENABLE_CUDA)
    list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/content/ai/ml/cuda_ops/")
endif()
if(NOT ENABLE_AI)
    list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/content/ai/")
    list(FILTER ENGINE_SRC_H EXCLUDE REGEX "/src/modules/module_ai_ml_helpers\\.h$")
endif()

set(ENGINE_PROJECT_CC ${ENGINE_SRC_CC} ${ENGINE_SRC_CU})
set(ENGINE_PROJECT_H  ${ENGINE_SRC_H})
