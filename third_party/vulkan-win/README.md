# vulkan-1 import library (MinGW, x86-64)

Windows has no `libvulkan` to link against — an application links an *import
library* that resolves each entry point to `vulkan-1.dll`, which the Vulkan
loader installs with the graphics driver. There is no such library in the
cross toolchain, so it is generated here from the Vulkan headers themselves:

    D=/home/jay/workspace/JFramework/include/vulkan
    { echo "LIBRARY vulkan-1.dll"; echo "EXPORTS"
      grep -ohE "VKAPI_CALL (vk[A-Za-z0-9]+)" $D/vulkan_core.h $D/vulkan_win32.h \
        | awk '{print $2}' | sort -u; } > vulkan-1.def
    x86_64-w64-mingw32-dlltool -d vulkan-1.def -l lib/libvulkan-1.a -D vulkan-1.dll

`vulkan_win32.h` is not optional: the swapchain is created through
`vkCreateWin32SurfaceKHR`, which lives only in that header, and leaving it out
compiles cleanly and fails at link with one undefined symbol.

Generated from the headers rather than curated by hand ON PURPOSE. The previous
import library in the JFramework tree held only the 79 entry points the
framework happened to call when it was made, so the first time the framework
called one more — `vkFreeDescriptorSets`, from font-atlas teardown — the link
broke with nothing to say why. Every function the headers declare is here, so
that cannot happen again.
