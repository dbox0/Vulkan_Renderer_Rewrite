#include "Parallel.h"

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace core {
    void parallelFor(uint32_t count, const std::function<void(uint32_t)> &body)
    {
        if (count == 0) {
            return;
        }
        const uint32_t workers = std::min(count, std::max(1u, std::thread::hardware_concurrency()));
        std::atomic<uint32_t> next{ 0 };
        const auto run = [&] {
            for (uint32_t i = next.fetch_add(1); i < count; i = next.fetch_add(1)) {
                body(i);
            }
        };

        std::vector<std::jthread> threads;
        threads.reserve(workers - 1);
        for (uint32_t t = 1; t < workers; ++t) {
            threads.emplace_back(run);
        }
        run();
    }
}
