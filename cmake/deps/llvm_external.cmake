if(NOT ENABLE_V8 OR NOT XER_FETCH_DEPENDENCIES OR NOT XER_BUILD_LLVM)
    return()
endif()

include(ExternalProject)

set(XER_LLVM_VERSION "llvmorg-18.1.8" CACHE STRING "LLVM and Clang source version")
set(XER_LLVM_BUILD_JOBS "4" CACHE STRING "Parallel jobs for the LLVM/Clang source build")
set(_xer_llvm_source "${XER_DEPS_DIR}/llvm-src")
set(_xer_llvm_build "${XER_DEPS_DIR}/llvm-build")
set(_xer_llvm_install "${XER_DEPS_DIR}/llvm-install")

if(NOT LLVM_DIR)
    set(LLVM_DIR "${_xer_llvm_install}/lib/cmake/llvm" CACHE PATH
        "LLVM CMake package installed by XER")
endif()
if(NOT Clang_DIR)
    set(Clang_DIR "${_xer_llvm_install}/lib/cmake/clang" CACHE PATH
        "Clang CMake package installed by XER")
endif()

ExternalProject_Add(XERLLVMExternal
    GIT_REPOSITORY https://github.com/llvm/llvm-project.git
    GIT_TAG "${XER_LLVM_VERSION}"
    GIT_SHALLOW TRUE
    SOURCE_DIR "${_xer_llvm_source}"
    BINARY_DIR "${_xer_llvm_build}"
    INSTALL_DIR "${_xer_llvm_install}"
    SOURCE_SUBDIR llvm
    CMAKE_ARGS
        "-DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>"
        -DCMAKE_BUILD_TYPE=Release
        -DLLVM_ENABLE_PROJECTS=clang
        -DLLVM_TARGETS_TO_BUILD=Native
        -DLLVM_ENABLE_ASSERTIONS=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_BUILD_TESTS=OFF
        -DLLVM_ENABLE_ZLIB=OFF
        -DLLVM_ENABLE_ZSTD=OFF
        -DLLVM_ENABLE_TERMINFO=OFF
        -DLLVM_ENABLE_LIBEDIT=OFF
        -DCLANG_ENABLE_ARCMT=OFF
        -DCLANG_ENABLE_STATIC_ANALYZER=OFF
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
        --config Release --parallel "${XER_LLVM_BUILD_JOBS}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
        --config Release --target install --parallel "${XER_LLVM_BUILD_JOBS}"
    BUILD_BYPRODUCTS
        "${_xer_llvm_install}/lib/cmake/llvm/LLVMConfig.cmake"
        "${_xer_llvm_install}/lib/cmake/clang/ClangConfig.cmake")