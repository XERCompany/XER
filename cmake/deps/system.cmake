# ============================================================
# Required system / compiler dependencies
# ============================================================

if(XER_FETCH_DEPENDENCIES)
    set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
    set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    xer_fetch_cmake_dependency(xer_abseil https://github.com/abseil/abseil-cpp.git 20250127.1)
    set(absl_FOUND TRUE)
else()
    find_package(absl QUIET)
endif()

if(NOT absl_FOUND)
    message(STATUS "absl package not found: using XER header-only compatibility fallback")
    set(XER_ABSL_COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/compat/absl/include")
    function(xer_add_absl_compat_target target_name)
        if(NOT TARGET ${target_name})
            string(REPLACE "::" "_" target_storage_name "${target_name}_compat")
            add_library(${target_storage_name} INTERFACE)
            target_include_directories(${target_storage_name} INTERFACE ${XER_ABSL_COMPAT_INCLUDE_DIR})
            add_library(${target_name} ALIAS ${target_storage_name})
        endif()
    endfunction()
    xer_add_absl_compat_target(absl::strings)
    xer_add_absl_compat_target(absl::str_format)
    xer_add_absl_compat_target(absl::hash)
    xer_add_absl_compat_target(absl::flat_hash_map)
endif()
find_package(LibXml2 QUIET)
find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)
find_package(LibNode QUIET MODULE)
set(ENGINE_HAS_LLVM_CLANG 0)
if(LLVM_FOUND AND Clang_FOUND)
    set(ENGINE_HAS_LLVM_CLANG 1)
elseif(ENABLE_V8)
    message(FATAL_ERROR "ENABLE_V8=ON requires LLVM and Clang CMake packages")
else()
    message(STATUS "LLVM/Clang packages not found: compiler runtime targets disabled with ENABLE_V8=OFF")
endif()
find_package(CURL QUIET)
find_package(OpenSSL QUIET)
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB QUIET)
endif()

if(ENABLE_V8)
    if(NOT LibXml2_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON requires LibXml2")
    endif()
    if(NOT CURL_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON requires CURL")
    endif()
    if(NOT OpenSSL_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON requires OpenSSL")
    endif()
    if(NOT ZLIB_FOUND)
        message(FATAL_ERROR "ENABLE_V8=ON requires ZLIB")
    endif()
endif()

# ── libnode / V8 ────────────────────────────────────────────
if(ENABLE_V8)
    if(NOT LibNode_FOUND)
        message(FATAL_ERROR
            "libnode not found: install its development package or set LibNode_ROOT (v8.h + libnode), or configure with ENABLE_V8=OFF")
    endif()
else()
    message(STATUS "ENABLE_V8=OFF: skipping libnode/V8 dependency discovery")
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
