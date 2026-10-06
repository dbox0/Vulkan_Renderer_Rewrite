# Vulkan Renderer Rewrite

Vulkan 1.3 renderer: SDL3, volk, VMA, shaderc, glm, tiny_gltf_v3, stb_image.
Loads a glTF scene and draws it with vertex pulling, bindless textures and multi-draw indirect.

This is a rewrite of https://github.com/dbox0/Vulkan_DynamicRendering

## Build and run

Needs SDL3, the Vulkan SDK (headers, loader, volk, validation layers), VMA, shaderc and glm.

    cmake --preset debug
    cmake --build --preset debug
    ./build/debug/bin/vulkanapp path/to/scene.gltf      # renderer only
    ./build/debug/bin/vulkaneditor [path/to/scene.gltf] # with the ImGui editor

The editor opens models from File > Open (Ctrl+O) or by dropping a .gltf/.glb on the window.
Configure with `-DBUILD_EDITOR=OFF` to build without it (and without fetching ImGui).
Extended editor functionality will gradually be added back in.

Presets: `debug`, `debug-asan`, `release`. Debug builds enable validation and synchronization validation.

