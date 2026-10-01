# Vulkan Renderer Rewrite

Vulkan 1.3 renderer: SDL3, volk, VMA, shaderc, glm, tiny_gltf_v3, stb_image.
Loads a glTF scene and draws it with vertex pulling, bindless textures and multi-draw indirect.

## Build and run

Needs SDL3, the Vulkan SDK (headers, loader, volk, validation layers), VMA, shaderc and glm.

    cmake --preset debug
    cmake --build --preset debug
    ./build/debug/app/vulkanapp path/to/scene.gltf

Presets: `debug`, `debug-asan`, `release`. Debug builds enable validation and synchronization validation.
Camera: W/S zoom, A/D orbit, Up/Down pitch. Esc quits.

## Layout

    engine/core     logging and fatal errors (no Vulkan)
    engine/gfx      context, swapchain, buffers, images, pipelines, barriers
    engine/scene    nodes, scene graph, camera, math (no Vulkan)
    engine/render   frame loop, geometry and resource stores
    engine/assets   glTF loader
    app/            window, events, main loop
    shaders/        GLSL, compiled at runtime from <exe dir>/shaders
    third_party/    tiny_gltf_v3, stb_image
    assets/         models and textures (kept out of src)

