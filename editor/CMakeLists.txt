include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9-docking)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3 Vulkan::Headers Vulkan::volk)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK VK_NO_PROTOTYPES)
target_compile_features(imgui PUBLIC cxx_std_20)

add_library(editor STATIC Editor.cpp)
target_include_directories(editor PUBLIC .)
target_link_libraries(editor PUBLIC app imgui)
enable_warnings(editor)

add_executable(vulkaneditor main.cpp)
target_link_libraries(vulkaneditor PRIVATE editor)
enable_warnings(vulkaneditor)
