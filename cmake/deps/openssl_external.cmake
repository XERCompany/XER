if(NOT XER_FETCH_DEPENDENCIES OR NOT ENABLE_V8)
    return()
endif()

include(ExternalProject)

set(XER_OPENSSL_VERSION "3.3.2" CACHE STRING "OpenSSL source version")
set(XER_OPENSSL_BUILD_JOBS "4" CACHE STRING "Parallel jobs for the OpenSSL source build")
set(_xer_openssl_install "${XER_DEPS_DIR}/openssl-install")
set(_xer_openssl_external_args
    GIT_REPOSITORY https://github.com/openssl/openssl.git
    GIT_TAG "openssl-${XER_OPENSSL_VERSION}"
    GIT_SHALLOW TRUE
    PREFIX "${XER_DEPS_DIR}/openssl-external"
    SOURCE_DIR "${XER_DEPS_DIR}/openssl-src"
    BINARY_DIR "${XER_DEPS_DIR}/openssl-build"
    INSTALL_DIR "${_xer_openssl_install}")

find_package(Perl QUIET)
if(UNIX)
    find_program(XER_OPENSSL_MAKE_EXECUTABLE NAMES gmake make)
elseif(WIN32)
    find_program(XER_OPENSSL_NMAKE_EXECUTABLE NAMES nmake)
endif()

set(_xer_openssl_build_enabled FALSE)
if(UNIX AND PERL_FOUND AND XER_OPENSSL_MAKE_EXECUTABLE)
    find_package(Threads REQUIRED)
    list(APPEND _xer_openssl_external_args
        CONFIGURE_COMMAND "${PERL_EXECUTABLE}" "<SOURCE_DIR>/Configure"
            no-shared no-tests no-apps no-asm
            "--prefix=<INSTALL_DIR>"
            "--openssldir=<INSTALL_DIR>/ssl"
        BUILD_COMMAND "${XER_OPENSSL_MAKE_EXECUTABLE}" -j "${XER_OPENSSL_BUILD_JOBS}"
        INSTALL_COMMAND "${XER_OPENSSL_MAKE_EXECUTABLE}" install_sw
        BUILD_BYPRODUCTS
            "${_xer_openssl_install}/lib/libcrypto.a"
            "${_xer_openssl_install}/lib/libssl.a")
    set(_xer_openssl_build_enabled TRUE)
elseif(WIN32 AND PERL_FOUND AND XER_OPENSSL_NMAKE_EXECUTABLE)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_xer_openssl_target VC-WIN64A)
    else()
        set(_xer_openssl_target VC-WIN32)
    endif()
    list(APPEND _xer_openssl_external_args
        CONFIGURE_COMMAND "${PERL_EXECUTABLE}" "<SOURCE_DIR>/Configure"
            "${_xer_openssl_target}" no-shared no-tests no-apps no-asm
            "--prefix=<INSTALL_DIR>"
            "--openssldir=<INSTALL_DIR>/ssl"
        BUILD_COMMAND "${XER_OPENSSL_NMAKE_EXECUTABLE}"
        INSTALL_COMMAND "${XER_OPENSSL_NMAKE_EXECUTABLE}" install_sw
        BUILD_BYPRODUCTS
            "${_xer_openssl_install}/lib/libcrypto.lib"
            "${_xer_openssl_install}/lib/libssl.lib")
    set(_xer_openssl_build_enabled TRUE)
endif()

if(NOT _xer_openssl_build_enabled)
    message(STATUS "OpenSSL sources will be downloaded to ${XER_DEPS_DIR}/openssl-src; Perl and a native make tool are required to build them, using an installed OpenSSL if available")
    list(APPEND _xer_openssl_external_args
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E echo "OpenSSL configure skipped: Perl/native make tool is unavailable"
        BUILD_COMMAND "${CMAKE_COMMAND}" -E echo "OpenSSL build skipped: Perl/native make tool is unavailable"
        INSTALL_COMMAND "${CMAKE_COMMAND}" -E echo "OpenSSL install skipped: Perl/native make tool is unavailable")
endif()

ExternalProject_Add(XEROpenSSLExternal ${_xer_openssl_external_args})

if(_xer_openssl_build_enabled)
    if(WIN32)
        set(_xer_openssl_crypto "${_xer_openssl_install}/lib/libcrypto.lib")
        set(_xer_openssl_ssl "${_xer_openssl_install}/lib/libssl.lib")
    else()
        set(_xer_openssl_crypto "${_xer_openssl_install}/lib/libcrypto.a")
        set(_xer_openssl_ssl "${_xer_openssl_install}/lib/libssl.a")
    endif()

    add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::Crypto PROPERTIES
        IMPORTED_LOCATION "${_xer_openssl_crypto}"
        INTERFACE_INCLUDE_DIRECTORIES "${_xer_openssl_install}/include")
    add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
    set_target_properties(OpenSSL::SSL PROPERTIES
        IMPORTED_LOCATION "${_xer_openssl_ssl}"
        INTERFACE_INCLUDE_DIRECTORIES "${_xer_openssl_install}/include"
        INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)

    set(OpenSSL_FOUND TRUE)
    set(OPENSSL_FOUND TRUE)
    set(OPENSSL_INCLUDE_DIR "${_xer_openssl_install}/include")
    set(OPENSSL_CRYPTO_LIBRARY "${_xer_openssl_crypto}")
    set(OPENSSL_SSL_LIBRARY "${_xer_openssl_ssl}")
    set(XER_OPENSSL_SOURCE_ENABLED TRUE)
endif()