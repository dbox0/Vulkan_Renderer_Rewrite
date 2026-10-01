#pragma once
#include <volk.h>
#include <vk_mem_alloc.h>

#include <format>
#include <string>

#include "core/Log.h"

// vk_enum_string_helper.h ships with Vulkan-Utility-Libraries, which not every system has.
#if __has_include(<vulkan/vk_enum_string_helper.h>)
#include <vulkan/vk_enum_string_helper.h>
inline std::string vkResultName(VkResult result) { return string_VkResult(result); }
#else
inline std::string vkResultName(VkResult result) { return std::format("VkResult {}", static_cast<int>(result)); }
#endif

#define VK_CHECK(expr)                                                                      \
    do {                                                                                    \
        const VkResult vkCheckResult = (expr);                                              \
        if (vkCheckResult != VK_SUCCESS) {                                                  \
            core::fatal(std::format("{} returned {}", #expr, vkResultName(vkCheckResult))); \
        }                                                                                   \
    } while (false)
