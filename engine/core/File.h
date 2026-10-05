#pragma once
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <span>
#include <vector>

namespace core
{

inline std::optional<std::vector<std::byte>> readFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(bytes.data()), size);
    if (!file) {
        return std::nullopt;
    }
    return bytes;
}

inline bool writeFileAtomic(const std::filesystem::path &path, std::span<const std::byte> bytes)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::random_device random;
    const std::filesystem::path temp = path.string() + std::format(".{:08x}{:08x}.tmp", random(), random());
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

}