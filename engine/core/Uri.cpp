#include "Uri.h"

#include <array>
#include <cstdint>

namespace core {
    namespace {
        constexpr std::array<int8_t, 256> makeBase64Table()
        {
            std::array<int8_t, 256> table{};
            table.fill(-1);
            for (int i = 0; i < 26; ++i) {
                table[static_cast<size_t>('A' + i)] = static_cast<int8_t>(i);
                table[static_cast<size_t>('a' + i)] = static_cast<int8_t>(26 + i);
            }
            for (int i = 0; i < 10; ++i) {
                table[static_cast<size_t>('0' + i)] = static_cast<int8_t>(52 + i);
            }
            table[static_cast<size_t>('+')] = 62;
            table[static_cast<size_t>('/')] = 63;
            return table;
        }

        constexpr std::array<int8_t, 256> Base64Table = makeBase64Table();

        int hexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }
    }

    std::optional<std::vector<std::byte>> base64Decode(std::string_view text)
    {
        std::vector<std::byte> out;
        out.reserve(text.size() / 4 * 3);

        uint32_t buffer = 0;
        int bits = 0;
        for (const char c : text) {
            if (c == '=') {
                break;
            }
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                continue;
            }
            const int8_t value = Base64Table[static_cast<unsigned char>(c)];
            if (value < 0) {
                return std::nullopt;
            }
            buffer = (buffer << 6) | static_cast<uint32_t>(value);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out.push_back(static_cast<std::byte>((buffer >> bits) & 0xFFu));
            }
        }
        return out;
    }

    std::string percentDecode(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '%' && i + 2 < text.size()) {
                const int high = hexValue(text[i + 1]);
                const int low  = hexValue(text[i + 2]);
                if (high >= 0 && low >= 0) {
                    out.push_back(static_cast<char>(high * 16 + low));
                    i += 2;
                    continue;
                }
            }
            out.push_back(text[i]);
        }
        return out;
    }
}
