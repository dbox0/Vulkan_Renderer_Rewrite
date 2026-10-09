#include "Mips.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace assets {
    namespace {
        float decodeSrgb(float c)
        {
            return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }

        uint8_t encodeSrgb(float linear)
        {
            const float c = std::clamp(linear, 0.0f, 1.0f);
            const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(std::lround(s * 255.0f));
        }

        const std::array<float, 256> &srgbToLinear()
        {
            static const std::array<float, 256> table = [] {
                std::array<float, 256> values{};
                for (size_t i = 0; i < values.size(); ++i) {
                    values[i] = decodeSrgb(static_cast<float>(i) / 255.0f);
                }
                return values;
            }();
            return table;
        }

        //2x2 Box Filter. Downsample by divifing w/h by 2
        MipLevel downsample(const MipLevel &source, ColorSpace space)
        {
            MipLevel target;
            target.width  = std::max(1u, source.width / 2);
            target.height = std::max(1u, source.height / 2);
            target.rgba.resize(size_t{ target.width } * target.height * 4);

            const std::array<float, 256> &toLinear = srgbToLinear();
            const bool srgb = space == ColorSpace::Srgb;
            const auto texel = [&](uint32_t x, uint32_t y) {
                return source.rgba.data() + (size_t{ y } * source.width + x) * 4;
            };

            for (uint32_t y = 0; y < target.height; ++y) {
                const uint32_t y0 = std::min(2 * y, source.height - 1);
                const uint32_t y1 = std::min(2 * y + 1, source.height - 1);
                for (uint32_t x = 0; x < target.width; ++x) {
                    const uint32_t x0 = std::min(2 * x, source.width - 1);
                    const uint32_t x1 = std::min(2 * x + 1, source.width - 1);
                    const std::array<const uint8_t *, 4> samples{ texel(x0, y0), texel(x1, y0), texel(x0, y1), texel(x1, y1) };
                    uint8_t *out = target.rgba.data() + (size_t{ y } * target.width + x) * 4;

                    for (size_t c = 0; c < 4; ++c) {
                        if (srgb && c < 3) {
                            float sum = 0.0f;
                            for (const uint8_t *s : samples) {
                                sum += toLinear[s[c]];
                            }
                            out[c] = encodeSrgb(sum * 0.25f);
                        } else {
                            uint32_t sum = 2;
                            for (const uint8_t *s : samples) {
                                sum += s[c];
                            }
                            out[c] = static_cast<uint8_t>(sum / 4);
                        }
                    }
                }
            }
            return target;
        }
    }

    std::vector<MipLevel> generateMips(const uint8_t *rgba, uint32_t width, uint32_t height, ColorSpace space)
    {
        const auto levels = static_cast<size_t>(std::floor(std::log2(std::max(width, height)))) + 1;
        std::vector<MipLevel> mips;
        mips.reserve(levels);
        mips.push_back(MipLevel{ .width = width, .height = height,
                                 .rgba = std::vector<uint8_t>(rgba, rgba + size_t{ width } * height * 4) });
        while (mips.back().width > 1 || mips.back().height > 1) {
            MipLevel next = downsample(mips.back(), space);
            mips.push_back(std::move(next));
        }
        return mips;
    }
}
