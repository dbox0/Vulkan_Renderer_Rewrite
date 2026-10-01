function(enable_warnings target)
    # Designated initializers leave Vulkan struct fields zeroed on purpose, so that warning stays off.
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-missing-field-initializers>)
endfunction()
