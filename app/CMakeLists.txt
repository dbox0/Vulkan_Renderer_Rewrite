add_executable(vulkanapp
    main.cpp
    Application.cpp)
target_link_libraries(vulkanapp PRIVATE assets render SDL3::SDL3)
enable_warnings(vulkanapp)

# Shaders are compiled at runtime from <exe dir>/shaders.
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:vulkanapp>/shaders)
add_dependencies(vulkanapp copy_shaders)
