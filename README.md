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
Extended functionality will gradually be added back in.

Presets: `debug`, `debug-asan`, `release`. Debug builds enable validation and synchronization validation.
Press V to cycle present modes; FIFO caps the frame rate at the display's refresh rate.

## Layout

    engine/core     logging and fatal errors (no Vulkan)
    engine/gfx      context, swapchain, buffers, images, pipelines, barriers
    engine/scene    nodes, scene graph, camera (no Vulkan)
    engine/render   frame loop, geometry and resource stores
    engine/assets   glTF loader
    app/            window, events, main loop, AppLayer hook (vulkanapp)
    editor/         ImGui editor, attaches as an AppLayer (vulkaneditor)
    shaders/        GLSL, compiled at runtime from <bin>/shaders
    third_party/    tiny_gltf_v3, stb_image
    assets/         models and textures (kept out of src)

Dependencies point down: core <- gfx <- render <- assets <- app <- editor, and core <- scene <- render.