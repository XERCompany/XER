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

# libxml2 is already fetched in fetch.cmake for ENABLE_V8

# zstd is already fetched in fetch.cmake for ENABLE_MEDIA or (ENABLE_V8 AND XER_BUILD_LLVM)

# zlib is already fetched in fetch.cmake

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
# LLVM/Clang detection
# ============================================================
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
