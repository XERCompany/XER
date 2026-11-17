# Shim: VulkanMemoryAllocator (GPUOpen VMA) is a single header,
# vk_mem_alloc.h — header-only usage (VMA_IMPLEMENTATION is defined by the
# consumer). vcpkg exposes it as GPUOpen::VulkanMemoryAllocator with the
# include dir pointed at include/vma so <vk_mem_alloc.h> resolves; probe
# both layouts so either spelling works.
# On Linux/macOS the partial-vcpkg share may provide the real port config;
# defer to it so this shim never shadows an authoritative config.
set(XER_DEFER_PKG "VulkanMemoryAllocator")
include("${CMAKE_CURRENT_LIST_DIR}/../xer_defer_vcpkg_config.cmake")

if(NOT XER_DEFER_CONFIG_LOADED)
    # <inc>/vk_mem_alloc.h  (flat)  and  <inc>/vma/vk_mem_alloc.h (nested,
    # the vcpkg layout) — collect whichever roots exist.
    find_path(XER_VMA_INCLUDE_DIR_FLAT NAMES vk_mem_alloc.h)
    find_path(XER_VMA_INCLUDE_DIR_NESTED NAMES vma/vk_mem_alloc.h)
    set(_xer_vma_inc "${XER_VMA_INCLUDE_DIR_FLAT}")
    if(XER_VMA_INCLUDE_DIR_NESTED)
        # find_path returns the dir containing vma/, i.e. the include root;
        # consumers may write <vk_mem_alloc.h> or <vma/vk_mem_alloc.h>.
        list(APPEND _xer_vma_inc "${XER_VMA_INCLUDE_DIR_NESTED}")
        list(APPEND _xer_vma_inc "${XER_VMA_INCLUDE_DIR_NESTED}/vma")
    endif()
    list(REMOVE_DUPLICATES _xer_vma_inc)
    list(FILTER _xer_vma_inc EXCLUDE REGEX "NOTFOUND$")

    if(_xer_vma_inc)
        if(NOT TARGET GPUOpen::VulkanMemoryAllocator)
            add_library(GPUOpen::VulkanMemoryAllocator INTERFACE IMPORTED GLOBAL)
            set_target_properties(GPUOpen::VulkanMemoryAllocator PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${_xer_vma_inc}")
        endif()
        set(VulkanMemoryAllocator_FOUND TRUE)
    else()
        set(VulkanMemoryAllocator_FOUND FALSE)
    endif()
else()
    set(VulkanMemoryAllocator_FOUND TRUE)
endif()

unset(_xer_vma_inc)
