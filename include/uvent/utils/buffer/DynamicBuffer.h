#ifndef UVENT_DYNAMICBUFFER_H
#define UVENT_DYNAMICBUFFER_H

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace usub::uvent::utils
{
    class DynamicBuffer final
    {
    public:
        DynamicBuffer() = default;

        void reserve(size_t n);

        [[nodiscard]] size_t size() const noexcept;
        [[nodiscard]] size_t capacity() const noexcept;

        [[nodiscard]] const uint8_t* data() const noexcept;
        [[nodiscard]] uint8_t* data() noexcept;

        void clear() noexcept;

        uint8_t* reserve_tail(size_t len);
        void commit(size_t n);

        void append(const uint8_t* src, size_t len);
        uint8_t* append_raw(size_t len);

        void shrink(size_t new_size) noexcept;

    private:
        void grow_(size_t need);

    private:
        std::vector<uint8_t> data_;
        size_t size_{0};
    };
}

#endif // UVENT_DYNAMICBUFFER_H