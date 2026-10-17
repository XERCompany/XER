include(FetchContent)

function(xer_fetch_cmake_dependency name repository tag)
    if(NOT XER_FETCH_DEPENDENCIES)
        return()
    endif()

    FetchContent_Declare(${name}
        GIT_REPOSITORY "${repository}"
        GIT_TAG "${tag}"
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE)
    FetchContent_MakeAvailable(${name})
endfunction()

if(XER_FETCH_DEPENDENCIES)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(xer_zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(xer_zlib)
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
        set(ZLIB_FOUND TRUE)
        set(ZLIB_INCLUDE_DIRS
            "${xer_zlib_SOURCE_DIR}"
            "${xer_zlib_BINARY_DIR}")
        set(ZLIB_LIBRARIES zlibstatic)
    endif()

    if(ENABLE_MEDIA)
        set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
        FetchContent_Declare(xer_zstd
            GIT_REPOSITORY https://github.com/facebook/zstd.git
            GIT_TAG v1.5.6
            GIT_SHALLOW TRUE
            SOURCE_SUBDIR build/cmake)
        FetchContent_MakeAvailable(xer_zstd)

        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(xer_brotli
            GIT_REPOSITORY https://github.com/google/brotli.git
            GIT_TAG v1.1.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(xer_brotli)
    endif()

    set(PTHREADPOOL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PTHREADPOOL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(FXDIV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FXDIV_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(xer_fxdiv
        GIT_REPOSITORY https://github.com/Maratyszcza/FXdiv.git
        GIT_TAG 63058eff77e11aa15bf531df5dd34395ec3017c8)
    FetchContent_MakeAvailable(xer_fxdiv)

    set(FXDIV_SOURCE_DIR "${xer_fxdiv_SOURCE_DIR}" CACHE PATH "FXdiv source directory" FORCE)
    FetchContent_Declare(xer_pthreadpool
        GIT_REPOSITORY https://github.com/Maratyszcza/pthreadpool.git
        GIT_TAG 560c60d342a76076f0557a3946924c6478470044)
    FetchContent_MakeAvailable(xer_pthreadpool)

    if(ENABLE_V8)
        set(LIBUV_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LIBUV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
        xer_fetch_cmake_dependency(xer_libuv https://github.com/libuv/libuv.git v1.49.2)
        set(libuv_FOUND TRUE)
    endif()
endif()