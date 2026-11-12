# XER Native Plugin Subsystem CMake Configuration
#
# Native (pxer) plugin host: loads third-party plugin binaries through
# dlopen/dlsym and dispatches plugin API calls via libffi closures.
# Requires libffi — the pxer host source is skipped when it is absent.

find_package(unofficial-libffi CONFIG QUIET)

if(TARGET unofficial::libffi::libffi)
    add_library(XER_Native STATIC
        plugin/pxer/pxer_host.cc
        plugin/pxer/pxer_host.h
        plugin/pxer/pxer_plugin_api.h
        plugin/pxer/pxer_global_hooks.h
    )

    target_include_directories(XER_Native
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(XER_Native
        PUBLIC
            unofficial::libffi::libffi
            absl::strings
            fmt::fmt
            XER_AsyncIO
            XER_Helper
    )

    target_compile_definitions(XER_Native PUBLIC XER_NATIVE_HAS_LIBFFI=1)
    target_compile_features(XER_Native PUBLIC cxx_std_23)

    if(MSVC)
        target_compile_options(XER_Native PRIVATE /W4 /WX- /O2)
    else()
        target_compile_options(XER_Native PRIVATE -Wall -Wextra -Wpedantic -O3)
    endif()
else()
    message(WARNING "libffi not found — XER_Native disabled")
endif()
