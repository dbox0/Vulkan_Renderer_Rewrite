#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace core
{

// Ring buffer of the last few seconds of CPU frame times, in milliseconds.
class FrameStats
{
public:
    static constexpr size_t Capacity = 240;

    void add(float milliseconds)
    {
        m_samples[m_next] = milliseconds;
        m_next = (m_next + 1) % Capacity;
        m_count = std::min(m_count + 1, Capacity);
    }

    const float *data() const { return m_samples.data(); }
    size_t count() const { return m_count; }
    size_t oldest() const { return m_count < Capacity ? 0 : m_next; }

    float average() const
    {
        if (m_count == 0) {
            return 0.0f;
        }
        float sum = 0.0f;
        for (size_t i = 0; i < m_count; ++i) {
            sum += m_samples[i];
        }
        return sum / static_cast<float>(m_count);
    }

    float maximum() const
    {
        return m_count == 0 ? 0.0f : *std::max_element(m_samples.begin(), m_samples.begin() + static_cast<std::ptrdiff_t>(m_count));
    }

    uint64_t uploadBytes = 0;
    uint32_t uploadStalls = 0;
    uint64_t uploadStallNs = 0;

private:
    std::array<float, Capacity> m_samples{};
    size_t m_next  = 0;
    size_t m_count = 0;

};

}
