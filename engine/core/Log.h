#pragma once
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace core
{

inline void log(std::string_view message)
{
    std::fprintf(stdout, "[info] %.*s\n", static_cast<int>(message.size()), message.data());
}

inline void warn(std::string_view message)
{
    std::fprintf(stderr, "[warn] %.*s\n", static_cast<int>(message.size()), message.data());
}

[[noreturn]] inline void fatal(std::string_view message)
{
    std::fprintf(stderr, "[fatal] %.*s\n", static_cast<int>(message.size()), message.data());
    std::abort();
}

} // namespace core
