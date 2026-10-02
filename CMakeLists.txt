cmake_minimum_required(VERSION 3.25)
project(VulkanRenderer LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(BUILD_EDITOR "Build the ImGui editor (vulkaneditor)" ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED COMPONENTS volk)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc)

include(cmake/Warnings.cmake)

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(app)
if(BUILD_EDITOR)
    add_subdirectory(editor)
endif()

# Shaders are compiled at runtime from <bin>/shaders.
add_custom_target(copy_shaders ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/shaders ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/shared ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/shared)
add_dependencies(vulkanapp copy_shaders)
if(BUILD_EDITOR)
    add_dependencies(vulkaneditor copy_shaders)
endif()
