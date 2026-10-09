#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace core {
    std::optional<std::vector<std::byte>> base64Decode(std::string_view text);
    std::string percentDecode(std::string_view text);
}
