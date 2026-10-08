#pragma once
#include <map>
#include <optional>
#include <cstdint>


namespace core {

    struct Range {
        uint64_t offset = 0;
        uint64_t count = 0;
    };

    class RangeAllocator {
    public:
        explicit RangeAllocator(uint64_t capacity);

        std::optional<uint64_t> allocate(uint64_t count);


        void free(uint64_t offset, uint64_t count);

        uint64_t capacity() const {return m_capacity;}
        uint64_t used()     const {return m_used;}
        uint64_t largestFree() const;

    private:
        std::map<uint64_t, uint64_t> m_free; //offset, count
        uint64_t m_capacity = 0;
        uint64_t m_used     = 0;
    };
}


