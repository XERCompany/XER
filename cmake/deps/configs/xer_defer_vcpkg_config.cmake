# xer_defer_vcpkg_config.cmake — shared tail for cmake/deps/configs shims.
#
# Included by a shim <pkg>Config.cmake when its own probes cannot satisfy
# the package (or, for compiled packages, before probing at all). If a
# partial-vcpkg install is active — XER_VCPKG_ROOT + XER_VCPKG_TRIPLET are
# set by system_deps.cmake on Linux/macOS — this looks for the package's
# REAL config under
#
#   <XER_VCPKG_ROOT>/installed/<XER_VCPKG_TRIPLET>/share/<pkg>/
#
# and includes the first match, so a shim never shadows an authoritative
# vcpkg config that would have been found later in CMAKE_PREFIX_PATH.
# On MSYS2 (windows-clang-msys2 preset) XER_VCPKG_ROOT is normally unset or
# points at a triplet that was never installed, so every EXISTS check fails
# and this is a no-op.
#
# Contract: the includer sets XER_DEFER_PKG to the find_package() name
# (e.g. "unofficial-fxdiv", "RapidJSON"). Sets XER_DEFER_CONFIG_LOADED to
# TRUE when a foreign config file was included.

set(XER_DEFER_CONFIG_LOADED FALSE)

if(DEFINED XER_VCPKG_ROOT AND DEFINED XER_VCPKG_TRIPLET AND XER_VCPKG_ROOT)
    string(TOLOWER "${XER_DEFER_PKG}" _xer_defer_pkg_lower)
    # vcpkg share dirs are lowercase port names; config file names vary
    # (<pkg>Config.cmake on some ports, <pkg>-config.cmake on others, and
    # RapidJSON keeps its case inside a lowercase dir).
    foreach(_xer_defer_dir_name IN ITEMS "${XER_DEFER_PKG}" "${_xer_defer_pkg_lower}")
        set(_xer_defer_dir
            "${XER_VCPKG_ROOT}/installed/${XER_VCPKG_TRIPLET}/share/${_xer_defer_dir_name}")
        if(IS_DIRECTORY "${_xer_defer_dir}")
            foreach(_xer_defer_file IN ITEMS
                    "${XER_DEFER_PKG}Config.cmake"
                    "${XER_DEFER_PKG}-config.cmake"
                    "${_xer_defer_pkg_lower}Config.cmake"
                    "${_xer_defer_pkg_lower}-config.cmake")
                set(_xer_defer_cfg "${_xer_defer_dir}/${_xer_defer_file}")
                if(EXISTS "${_xer_defer_cfg}")
                    include("${_xer_defer_cfg}")
                    set(XER_DEFER_CONFIG_LOADED TRUE)
                    break()
                endif()
            endforeach()
        endif()
        if(XER_DEFER_CONFIG_LOADED)
            break()
        endif()
    endforeach()
endif()

unset(_xer_defer_dir_name)
unset(_xer_defer_dir)
unset(_xer_defer_file)
unset(_xer_defer_cfg)
unset(_xer_defer_pkg_lower)
