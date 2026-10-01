cmake_minimum_required(VERSION 3.25)
project(VulkanRenderer LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED COMPONENTS volk)
find_package(glm REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SHADERC REQUIRED IMPORTED_TARGET shaderc)

include(cmake/Warnings.cmake)

add_subdirectory(third_party)
add_subdirectory(engine)
add_subdirectory(app)
