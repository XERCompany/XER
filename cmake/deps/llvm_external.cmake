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

set(_xer_llvm_dependencies)
set(_xer_llvm_dependency_args)
if(TARGET zlibstatic)
    list(APPEND _xer_llvm_dependencies zlibstatic)
    list(APPEND _xer_llvm_dependency_args
        -DLLVM_ENABLE_ZLIB=ON
        "-DZLIB_INCLUDE_DIR=${xer_zlib_SOURCE_DIR}"
        "-DZLIB_LIBRARY=$<TARGET_FILE:zlibstatic>")
endif()
if(TARGET LibXml2)
    list(APPEND _xer_llvm_dependencies LibXml2)
    list(APPEND _xer_llvm_dependency_args
        -DLLVM_ENABLE_LIBXML2=ON
        "-DLIBXML2_INCLUDE_DIR=${xer_libxml2_BINARY_DIR}"
        "-DLIBXML2_LIBRARY=$<TARGET_FILE:LibXml2>")
    if(MSVC)
        list(APPEND _xer_llvm_dependency_args
            "-DCMAKE_C_FLAGS=/I${xer_libxml2_SOURCE_DIR}/include /I${xer_libxml2_BINARY_DIR}"
            "-DCMAKE_CXX_FLAGS=/I${xer_libxml2_SOURCE_DIR}/include /I${xer_libxml2_BINARY_DIR}")
    else()
        list(APPEND _xer_llvm_dependency_args
            "-DCMAKE_C_FLAGS=-I${xer_libxml2_SOURCE_DIR}/include -I${xer_libxml2_BINARY_DIR}"
            "-DCMAKE_CXX_FLAGS=-I${xer_libxml2_SOURCE_DIR}/include -I${xer_libxml2_BINARY_DIR}")
    endif()
endif()
if(TARGET libzstd_static)
    list(APPEND _xer_llvm_dependencies libzstd_static)
    list(APPEND _xer_llvm_dependency_args
        -DLLVM_ENABLE_ZSTD=ON
        "-Dzstd_INCLUDE_DIR=${xer_zstd_SOURCE_DIR}/lib"
        "-Dzstd_LIBRARY=$<TARGET_FILE:libzstd_static>")
endif()

ExternalProject_Add(XERLLVMExternal
    DEPENDS ${_xer_llvm_dependencies}
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
        ${_xer_llvm_dependency_args}
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