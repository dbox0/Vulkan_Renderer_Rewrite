add_library(third_party STATIC Implementations.cpp)
target_include_directories(third_party SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/tinygltf
    ${CMAKE_CURRENT_SOURCE_DIR}/stb)
target_compile_features(third_party PUBLIC cxx_std_20)
