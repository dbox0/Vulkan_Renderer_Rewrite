add_library(core STATIC
        core/Hash.h
        core/RangeAllocator.cpp
        core/RangeAllocator.h
        core/Handle.h
        core/Parallel.cpp
        core/Parallel.h
        core/Transform.h
        core/Uri.cpp
        core/Uri.h)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(core PUBLIC glm::glm Threads::Threads)
# Zero-to-one depth so glm's projections match Vulkan's clip space.
target_compile_definitions(core PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_ENABLE_EXPERIMENTAL)
target_compile_features(core PUBLIC cxx_std_20)

enable_warnings(core)

add_library(gfx STATIC
    gfx/Context.cpp
    gfx/Swapchain.cpp
    gfx/Pipeline.cpp
    gfx/ShaderCompiler.cpp
    gfx/Implementations.cpp
    gfx/Queue.cpp
    gfx/DeletionQueue.h
    gfx/FrameArena.cpp
    gfx/FrameArena.h
        gfx/StagingUploader.cpp
        gfx/StagingUploader.h
        gfx/GpuProfiler.cpp
        gfx/GpuProfiler.h
        gfx/ShaderCompiler.cpp
        gfx/ShaderCompiler.h)
target_link_libraries(gfx
    PUBLIC core Vulkan::Headers Vulkan::volk
    PRIVATE SDL3::SDL3 PkgConfig::SHADERC)
enable_warnings(gfx)

# CPU-side scene data. No Vulkan.
add_library(scene STATIC
    scene/Scene.cpp
    scene/Camera.cpp
        scene/SceneTypes.h
        scene/ComponentTable.h
        scene/Components.h)
target_link_libraries(scene PUBLIC core glm::glm SDL3::SDL3)
enable_warnings(scene)

add_library(render STATIC
    render/Renderer.cpp
    render/PipelineRegistry.cpp
    render/GeometryStore.cpp
    render/ResourceStore.cpp
        render/InstanceSlots.h
        render/InstanceSlots.cpp
        render/SceneExtract.cpp
        render/SceneExtract.h
        render/Instantiate.cpp
        render/Instantiate.h
        render/DeviceTable.h
        render/MaterialTable.cpp
        render/MaterialTable.h)
target_link_libraries(render PUBLIC gfx scene assets)
# shaders/shared/GpuTypes.h compiled by both C++ and GLSL.
target_include_directories(render PUBLIC ${PROJECT_SOURCE_DIR}/shaders)
enable_warnings(render)

# CPU-side import. No Vulkan.
add_library(assets STATIC
    assets/GltfImporter.cpp
    assets/ImportedScene.h
    assets/MeshData.h
    assets/Mips.cpp
    assets/Mips.h)
target_link_libraries(assets PUBLIC core PRIVATE third_party)
enable_warnings(assets)
