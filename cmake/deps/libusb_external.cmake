if(NOT XER_FETCH_DEPENDENCIES)
    return()
endif()

include(ExternalProject)

set(XER_LIBUSB_VERSION "1.0.29" CACHE STRING "libusb source version")
set(XER_LIBUSB_BUILD_JOBS "4" CACHE STRING "Parallel jobs for the libusb source build")
set(_xer_libusb_install "${XER_DEPS_DIR}/libusb-install")
set(_xer_libusb_external_args
    GIT_REPOSITORY https://github.com/libusb/libusb.git
    GIT_TAG "v${XER_LIBUSB_VERSION}"
    GIT_SHALLOW TRUE
    PREFIX "${XER_DEPS_DIR}/libusb-external"
    SOURCE_DIR "${XER_DEPS_DIR}/libusb-src"
    BINARY_DIR "${XER_DEPS_DIR}/libusb-build"
    INSTALL_DIR "${_xer_libusb_install}")

if(UNIX)
    find_program(XER_LIBUSB_MAKE_EXECUTABLE NAMES gmake make)
    find_program(XER_LIBUSB_AUTORECONF_EXECUTABLE NAMES autoreconf)
endif()

if(UNIX AND XER_LIBUSB_MAKE_EXECUTABLE AND XER_LIBUSB_AUTORECONF_EXECUTABLE)
    list(APPEND _xer_libusb_external_args
        CONFIGURE_COMMAND "<SOURCE_DIR>/configure"
            "--prefix=<INSTALL_DIR>"
            --enable-static
            --disable-shared
            --disable-examples-build
            --disable-tests-build
            $<$<PLATFORM_ID:Linux>:--disable-udev>
        BUILD_COMMAND "${XER_LIBUSB_MAKE_EXECUTABLE}" -j "${XER_LIBUSB_BUILD_JOBS}"
        INSTALL_COMMAND "${XER_LIBUSB_MAKE_EXECUTABLE}" install
        BUILD_BYPRODUCTS "${_xer_libusb_install}/lib/libusb-1.0.a")

    ExternalProject_Add(XERLibusbExternal ${_xer_libusb_external_args})
    ExternalProject_Add_Step(XERLibusbExternal bootstrap
        COMMAND "${CMAKE_COMMAND}" -E env NOCONFIGURE=1
            "${CMAKE_COMMAND}" -E chdir <SOURCE_DIR> sh autogen.sh
        DEPENDEES download
        DEPENDERS configure)

    add_library(XERLibusbLibraries INTERFACE)
    target_include_directories(XERLibusbLibraries INTERFACE
        "${_xer_libusb_install}/include/libusb-1.0")
    target_link_libraries(XERLibusbLibraries INTERFACE
        "${_xer_libusb_install}/lib/libusb-1.0.a")
    set(LIBUSB_INCLUDE_DIR "${_xer_libusb_install}/include/libusb-1.0")
    set(LIBUSB_LIBRARY XERLibusbLibraries)
    set(XER_LIBUSB_SOURCE_ENABLED TRUE)
else()
    ExternalProject_Add(XERLibusbExternal ${_xer_libusb_external_args}
        CONFIGURE_COMMAND "${CMAKE_COMMAND}" -E echo "libusb configure skipped: POSIX Autotools/Make are unavailable"
        BUILD_COMMAND "${CMAKE_COMMAND}" -E echo "libusb build skipped: POSIX Autotools/Make are unavailable"
        INSTALL_COMMAND "${CMAKE_COMMAND}" -E echo "libusb install skipped: POSIX Autotools/Make are unavailable")
    message(STATUS "libusb source will be downloaded to ${XER_DEPS_DIR}/libusb-src; using an installed libusb binary when available")
endif()