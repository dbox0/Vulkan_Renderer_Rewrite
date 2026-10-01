add_library(app STATIC Application.cpp)
target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(app PUBLIC assets render SDL3::SDL3)
enable_warnings(app)

add_executable(vulkanapp main.cpp)
target_link_libraries(vulkanapp PRIVATE app)
enable_warnings(vulkanapp)
