# Shim: VulkanUtilityLibraries (KhronosGroup/Vulkan-Utility-Libraries) —
# header-mostly helpers; the vcpkg port also builds two small static libs
# (VulkanSafeStruct, VulkanLayerSettings). MSYS2 ships the same headers,
# possibly without the compiled libs — fall back to interface targets and
# let the spike verify link needs.
#
# Target contract mirrors the vcpkg port:
#   Vulkan::SafeStruct / Vulkan::LayerSettings  (static libs when present)
#   Vulkan::UtilityHeaders / Vulkan::CompilerConfiguration (interface)
#   all link Vulkan::Headers (created here when vulkan-headers has no
#   CMake config of its own, e.g. MSYS2).
#
# VERIFY-on-spike: whether the MSYS2 package builds libVulkanSafeStruct /
# libVulkanLayerSettings, and exact header layout under include/vulkan/.
set(XER_DEFER_PKG "VulkanUtilityLibraries")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    # VUL marker headers live at <inc>/vulkan/utility|layer/*.hpp; the
    # vulkan-headers C++ headers at <inc>/vulkan/vulkan*.hpp(h).
    find_path(XER_VUL_INCLUDE_DIR NAMES vulkan/utility/vk_safe_struct.hpp)
    find_path(XER_VUL_VK_HEADERS_DIR NAMES vulkan/vulkan_core.h)

    if(XER_VUL_INCLUDE_DIR)
        # Vulkan::Headers — normally provided by the vulkan-headers port;
        # create it here when absent so VUL targets have something to link.
        if(NOT TARGET Vulkan::Headers)
            if(XER_VUL_VK_HEADERS_DIR)
                add_library(Vulkan::Headers INTERFACE IMPORTED GLOBAL)
                set_target_properties(Vulkan::Headers PROPERTIES
                    INTERFACE_INCLUDE_DIRECTORIES "${XER_VUL_VK_HEADERS_DIR}")
            endif()
        endif()

        set(_xer_vul_inc "${XER_VUL_INCLUDE_DIR}")
        if(XER_VUL_VK_HEADERS_DIR)
            list(APPEND _xer_vul_inc "${XER_VUL_VK_HEADERS_DIR}")
        endif()
        list(REMOVE_DUPLICATES _xer_vul_inc)

        # CompilerConfiguration: mirror the vcpkg compile definitions;
        # the MSVC-only warning flags are intentionally dropped.
        if(NOT TARGET Vulkan::CompilerConfiguration)
            add_library(Vulkan::CompilerConfiguration INTERFACE IMPORTED GLOBAL)
            set_target_properties(Vulkan::CompilerConfiguration PROPERTIES
                INTERFACE_COMPILE_DEFINITIONS
                    "VK_ENABLE_BETA_EXTENSIONS;NOMINMAX;WIN32_LEAN_AND_MEAN;VK_USE_PLATFORM_WIN32_KHR"
                INTERFACE_INCLUDE_DIRECTORIES "${_xer_vul_inc}")
        endif()

        if(NOT TARGET Vulkan::UtilityHeaders)
            add_library(Vulkan::UtilityHeaders INTERFACE IMPORTED GLOBAL)
            set(_xer_vul_uh_links "")
            if(TARGET Vulkan::Headers)
                set(_xer_vul_uh_links "Vulkan::Headers")
            endif()
            set_target_properties(Vulkan::UtilityHeaders PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_xer_vul_inc}"
                INTERFACE_LINK_LIBRARIES "${_xer_vul_uh_links}")
        endif()

        # Link list shared by the compiled-lib and interface fallbacks.
        # Vulkan::Headers only exists when vulkan_core.h was located —
        # never reference a target that was not created.
        set(_xer_vul_base_links "Vulkan::UtilityHeaders;$<LINK_ONLY:Vulkan::CompilerConfiguration>")
        if(TARGET Vulkan::Headers)
            set(_xer_vul_base_links "Vulkan::Headers;${_xer_vul_base_links}")
        endif()

        # SafeStruct / LayerSettings: prefer the compiled libs (vcpkg
        # parity), degrade to interface targets when only headers exist.
        foreach(_xer_vul_lib IN ITEMS
                "Vulkan::SafeStruct|VulkanSafeStruct"
                "Vulkan::LayerSettings|VulkanLayerSettings")
            string(REPLACE "|" ";" _xer_vul_parts "${_xer_vul_lib}")
            list(GET _xer_vul_parts 0 _xer_vul_target)
            list(GET _xer_vul_parts 1 _xer_vul_libname)
            if(NOT TARGET ${_xer_vul_target})
                # Distinct cache var per lib — reusing one name would make
                # the second find_library() return the first lib's path.
                string(MAKE_C_IDENTIFIER "_xer_vul_lib_${_xer_vul_libname}"
                    _xer_vul_lib_var)
                find_library(${_xer_vul_lib_var} NAMES "${_xer_vul_libname}")
                if(${_xer_vul_lib_var})
                    add_library(${_xer_vul_target} UNKNOWN IMPORTED GLOBAL)
                    set_target_properties(${_xer_vul_target} PROPERTIES
                        IMPORTED_LOCATION "${${_xer_vul_lib_var}}"
                        INTERFACE_INCLUDE_DIRECTORIES "${_xer_vul_inc}"
                        INTERFACE_LINK_LIBRARIES "${_xer_vul_base_links}")
                else()
                    add_library(${_xer_vul_target} INTERFACE IMPORTED GLOBAL)
                    set_target_properties(${_xer_vul_target} PROPERTIES
                        INTERFACE_INCLUDE_DIRECTORIES "${_xer_vul_inc}"
                        INTERFACE_LINK_LIBRARIES "${_xer_vul_base_links}")
                endif()
            endif()
        endforeach()

        set(VulkanUtilityLibraries_FOUND TRUE)
    else()
        set(VulkanUtilityLibraries_FOUND FALSE)
    endif()
else()
    set(VulkanUtilityLibraries_FOUND TRUE)
endif()

unset(_xer_vul_inc)
unset(_xer_vul_uh_links)
unset(_xer_vul_base_links)
unset(_xer_vul_lib)
unset(_xer_vul_parts)
unset(_xer_vul_target)
unset(_xer_vul_libname)
unset(_xer_vul_lib_var)
