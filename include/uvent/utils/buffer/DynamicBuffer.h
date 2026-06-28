#ifndef UVENT_DYNAMICBUFFER_H
#define UVENT_DYNAMICBUFFER_H

#pragma once

#include <cstddef>
#include <cstdint>

namespace usub::uvent::utils
{
    /// \brief Растущий байтовый буфер с уровнем cache-line alignment'а
    ///        и БЕЗ value-init'а памяти.
    ///
    /// Раньше использовал std::vector<uint8_t>, у которого resize(n) делает
    /// memset(0) свежей памяти. На сценарии echo-сервера это давало 64КБ memset
    /// на каждый принятый коннект (см. clientCoro: buffer.reserve(64*1024)).
    /// Эту память тут же перезаписывает recv() — нолики не нужны.
    class DynamicBuffer final
    {
    public:
        DynamicBuffer() = default;

        DynamicBuffer(const DynamicBuffer&) = delete;
        DynamicBuffer& operator=(const DynamicBuffer&) = delete;

        DynamicBuffer(DynamicBuffer&& o) noexcept
            : data_(o.data_), cap_(o.cap_), size_(o.size_)
        {
            o.data_ = nullptr;
            o.cap_  = 0;
            o.size_ = 0;
        }
        DynamicBuffer& operator=(DynamicBuffer&& o) noexcept
        {
            if (this != &o)
            {
                free_();
                data_   = o.data_;
                cap_    = o.cap_;
                size_   = o.size_;
                o.data_ = nullptr;
                o.cap_  = 0;
                o.size_ = 0;
            }
            return *this;
        }

        ~DynamicBuffer() { free_(); }

        void reserve(size_t n);

        [[nodiscard]] size_t size() const noexcept { return size_; }
        [[nodiscard]] size_t capacity() const noexcept { return cap_; }

        [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
        [[nodiscard]] uint8_t* data() noexcept { return data_; }

        void clear() noexcept { size_ = 0; }

        uint8_t* reserve_tail(size_t len);
        void commit(size_t n);

        void append(const uint8_t* src, size_t len);
        uint8_t* append_raw(size_t len);

        void shrink(size_t new_size) noexcept;

    private:
        void grow_(size_t need);
        void free_() noexcept;

    private:
        uint8_t* data_{nullptr};
        size_t   cap_{0};
        size_t   size_{0};
    };
}

#endif // UVENT_DYNAMICBUFFER_H