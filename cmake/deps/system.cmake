# ============================================================
# Required system / compiler dependencies
# ============================================================

# Abseil - always fetch via FetchContent for consistency
if(XER_FETCH_DEPENDENCIES)
    message(STATUS "Fetching Abseil via FetchContent")
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    xer_fetch_cmake_dependency(xer_abseil https://github.com/abseil/abseil-cpp.git 20250127.1)
    set(absl_FOUND TRUE)
else()
    find_package(absl QUIET)
    if(NOT absl_FOUND)
        message(FATAL_ERROR "Abseil not found. Please install Abseil or set XER_FETCH_DEPENDENCIES=ON to auto-fetch.")
    endif()
endif()

# ============================================================
# libxml2 auto-fetch (required for V8+LLVM/Clang)
# ============================================================
if(NOT LibXml2_FOUND AND XER_FETCH_DEPENDENCIES)
    message(STATUS "libxml2 not found, fetching via FetchContent")
    include(FetchContent)

    FetchContent_Declare(
        libxml2
        GIT_REPOSITORY https://github.com/GNOME/libxml2.git
        GIT_TAG v2.12.7
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/libxml2-src"
        BINARY_DIR "${XER_DEPS_DIR}/libxml2-build"
    )

    FetchContent_MakeAvailable(libxml2)

    if(EXISTS "${XER_DEPS_DIR}/libxml2-src/CMakeLists.txt")
        message(STATUS "Building libxml2 from source")
        option(LIBXML2_WITH_PROGRAMS "Build programs" OFF)
        option(LIBXML2_WITH_TESTS "Build tests" OFF)
        option(LIBXML2_WITH_PYTHON "Python bindings" OFF)
        add_subdirectory("${XER_DEPS_DIR}/libxml2-src" "${XER_DEPS_DIR}/libxml2-build" EXCLUDE_FROM_ALL)
        set(LibXml2_FOUND TRUE)
    else()
        message(WARNING "libxml2 auto-fetch failed. Install manually:")
        message(WARNING "  Windows: vcpkg install libxml2:x64-windows")
        message(WARNING "  Linux: apt install libxml2-dev")
        message(WARNING "  macOS: brew install libxml2")
    endif()
endif()

# ============================================================
# zstd auto-fetch (optional compression library)
# ============================================================
if(NOT zstd_FOUND AND XER_FETCH_DEPENDENCIES)
    message(STATUS "zstd not found, fetching via FetchContent")
    include(FetchContent)

    FetchContent_Declare(
        zstd
        GIT_REPOSITORY https://github.com/facebook/zstd.git
        GIT_TAG v1.5.6
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/zstd-src"
        BINARY_DIR "${XER_DEPS_DIR}/zstd-build"
    )

    FetchContent_MakeAvailable(zstd)

    if(EXISTS "${XER_DEPS_DIR}/zstd-src/build/cmake/CMakeLists.txt")
        message(STATUS "Building zstd from source")
        add_subdirectory("${XER_DEPS_DIR}/zstd-src/build/cmake" "${XER_DEPS_DIR}/zstd-build" EXCLUDE_FROM_ALL)
        set(zstd_FOUND TRUE)
    else()
        message(WARNING "zstd auto-fetch failed. Install manually:")
        message(WARNING "  Windows: vcpkg install zstd:x64-windows")
        message(WARNING "  Linux: apt install libzstd-dev")
        message(WARNING "  macOS: brew install zstd")
    endif()
endif()

# ============================================================
# zlib auto-fetch (required compression library)
# ============================================================
if(NOT ZLIB_FOUND AND XER_FETCH_DEPENDENCIES)
    message(STATUS "zlib not found, fetching via FetchContent")
    include(FetchContent)

    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/zlib-src"
        BINARY_DIR "${XER_DEPS_DIR}/zlib-build"
    )

    FetchContent_MakeAvailable(zlib)

    if(EXISTS "${XER_DEPS_DIR}/zlib-src/CMakeLists.txt")
        message(STATUS "Building zlib from source")
        add_subdirectory("${XER_DEPS_DIR}/zlib-src" "${XER_DEPS_DIR}/zlib-build" EXCLUDE_FROM_ALL)
        set(ZLIB_FOUND TRUE)
    else()
        message(WARNING "zlib auto-fetch failed. Install manually:")
        message(WARNING "  Windows: vcpkg install zlib:x64-windows")
        message(WARNING "  Linux: apt install zlib1g-dev")
        message(WARNING "  macOS: brew install zlib")
    endif()
endif()
if(NOT TARGET LibXml2::LibXml2)
    find_package(LibXml2 QUIET)
endif()
find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)
if(ENABLE_V8 AND NOT TARGET V8::V8)
    find_package(LibNode QUIET MODULE)
    if(LibNode_FOUND)
        add_library(V8::V8 INTERFACE IMPORTED GLOBAL)
        set_target_properties(V8::V8 PROPERTIES
            INTERFACE_LINK_LIBRARIES LibNode::LibNode
            INTERFACE_INCLUDE_DIRECTORIES "${LibNode_INCLUDE_DIR}")
        set(V8_FOUND TRUE)
    endif()
endif()
set(ENGINE_HAS_LLVM_CLANG 0)
if(LLVM_FOUND AND Clang_FOUND)
    set(ENGINE_HAS_LLVM_CLANG 1)
elseif(ENABLE_V8)
    message(STATUS "LLVM/Clang CMake packages not found: XER executable disabled; V8 runtime can still be built")
else()
    message(STATUS "LLVM/Clang packages not found: compiler runtime targets disabled with ENABLE_V8=OFF")
endif()
# ============================================================
# System library discovery
# ============================================================

# Optional: find system packages first
find_package(CURL QUIET)
find_package(OpenSSL QUIET)
find_package(LibXml2 QUIET)
find_package(libuv QUIET)
find_package(zstd QUIET)

# ============================================================
# libuv auto-fetch (if not found and XER_FETCH_DEPENDENCIES=ON)
# ============================================================
if(NOT TARGET libuv::libuv AND XER_FETCH_DEPENDENCIES)
    message(STATUS "libuv not found, fetching via FetchContent")
    include(FetchContent)

    FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.48.0
        GIT_SHALLOW TRUE
        SOURCE_DIR "${XER_DEPS_DIR}/libuv-src"
        BINARY_DIR "${XER_DEPS_DIR}/libuv-build"
    )

    FetchContent_MakeAvailable(libuv)

    if(EXISTS "${XER_DEPS_DIR}/libuv-src/CMakeLists.txt")
        message(STATUS "Building libuv from source")
        add_subdirectory("${XER_DEPS_DIR}/libuv-src" "${XER_DEPS_DIR}/libuv-build" EXCLUDE_FROM_ALL)
        set(libuv_FOUND TRUE)
    else()
        message(WARNING "libuv auto-fetch failed. Install manually:")
        message(WARNING "  Windows: vcpkg install libuv:x64-windows")
        message(WARNING "  Linux: apt install libuv1-dev")
        message(WARNING "  macOS: brew install libuv")
    endif()
endif()

# ============================================================
# LLVM/Clang detection
# ============================================================
find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)
set(ENGINE_HAS_LLVM_CLANG 0)
if(LLVM_FOUND AND Clang_FOUND)
    set(ENGINE_HAS_LLVM_CLANG 1)
    message(STATUS "LLVM/Clang found: XER executable will be built")
elseif(ENABLE_V8)
    message(STATUS "LLVM/Clang not found: V8 runtime will be built, but XER executable will be disabled")
else()
    message(STATUS "LLVM/Clang not found: compiler runtime targets disabled")
endif()

# ============================================================
# Dependency validation
# ============================================================
if(ENABLE_V8 AND ENGINE_HAS_LLVM_CLANG)
    if(NOT LibXml2_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON with LLVM/Clang requires LibXml2. "
            "Set XER_FETCH_DEPENDENCIES=ON to auto-fetch, or install LibXml2 manually.")
    endif()
    if(NOT CURL_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON with LLVM/Clang requires CURL")
    endif()
    if(NOT OpenSSL_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON with LLVM/Clang requires OpenSSL")
    endif()
    if(NOT ZLIB_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON with LLVM/Clang requires ZLIB")
    endif()
endif()

# Warn about missing optional dependencies
if(NOT OpenSSL_FOUND)
    message(WARNING "OpenSSL not found. Network module and some features will be limited.")
endif()

if(NOT libuv_FOUND)
    message(WARNING "libuv not found. V8 and network features may be limited.")
endif()

if(NOT zstd_FOUND)
    message(STATUS "zstd not found. Compression features may be limited.")
endif()

# ── V8 ───────────────────────────────────────────────────────
if(ENABLE_V8)
    if(NOT TARGET V8::V8)
        message(FATAL_ERROR
            "V8 not found: enable XER_FETCH_DEPENDENCIES to build V8 from source, or provide a V8::V8 or LibNode package")
    endif()
else()
    message(STATUS "ENABLE_V8=OFF: skipping V8 dependency discovery")
endif()

# ── LLVM component lib list ──────────────────────────────────
set(LLVM_ALL_COMPONENT_LIBS)
if(ENGINE_HAS_LLVM_CLANG)
    llvm_map_components_to_libnames(LLVM_ALL_COMPONENT_LIBS all)
endif()

# ── Clang available libs ─────────────────────────────────────
set(CLANG_ALL_TARGETS
    clang
    clang-cpp
    clangAPINotes
    clangARCMigrate
    clangAST
    clangASTMatchers
    clangAnalysis
    clangAnalysisFlowSensitive
    clangAnalysisFlowSensitiveModels
    clangApplyReplacements
    clangBasic
    clangChangeNamespace
    clangCodeGen
    clangCrossTU
    clangDaemon
    clangDaemonTweaks
    clangDependencyScanning
    clangDirectoryWatcher
    clangDoc
    clangDriver
    clangDynamicASTMatchers
    clangEdit
    clangExtractAPI
    clangFormat
    clangFrontend
    clangFrontendTool
    clangHandleCXX
    clangHandleLLVM
    clangIncludeCleaner
    clangIncludeFixer
    clangIncludeFixerPlugin
    clangIndex
    clangInterpreter
    clangLex
    clangMove
    clangParse
    clangPseudo
    clangPseudoCLI
    clangPseudoCXX
    clangPseudoGrammar
    clangPseudoMatchers
    clangPseudoTest
    clangReorderFields
    clangRewrite
    clangRewriteFrontend
    clangSema
    clangSerialization
    clangStaticAnalyzerCheckers
    clangStaticAnalyzerCore
    clangStaticAnalyzerFrontend
    clangSupport
    clangTidy
    clangTidyAbseilModule
    clangTidyAlteraModule
    clangTidyAndroidModule
    clangTidyBoostModule
    clangTidyBugproneModule
    clangTidyCERTModule
    clangTidyConcurrencyModule
    clangTidyCppCoreGuidelinesModule
    clangTidyDarwinModule
    clangTidyFuchsiaModule
    clangTidyGoogleModule
    clangTidyHICPPModule
    clangTidyLLVMLibcModule
    clangTidyLLVMModule
    clangTidyLinuxKernelModule
    clangTidyMPIModule
    clangTidyMain
    clangTidyMiscModule
    clangTidyModernizeModule
    clangTidyObjCModule
    clangTidyOpenMPModule
    clangTidyPerformanceModule
    clangTidyPlugin
    clangTidyPortabilityModule
    clangTidyReadabilityModule
    clangTidyUtils
    clangTidyZirconModule
    clangTooling
    clangToolingASTDiff
    clangToolingCore
    clangToolingInclusions
    clangToolingInclusionsStdlib
    clangToolingRefactoring
    clangToolingSyntax
    clangTransformer
)

set(CLANG_AVAILABLE_LIBS)
if(ENGINE_HAS_LLVM_CLANG)
    foreach(clang_target IN LISTS CLANG_ALL_TARGETS)
        if(TARGET ${clang_target})
            list(APPEND CLANG_AVAILABLE_LIBS ${clang_target})
        endif()
    endforeach()
endif()
