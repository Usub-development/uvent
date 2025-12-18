#include "uvent/utils/buffer/DynamicBuffer.h"

#include <cstring>

namespace usub::uvent::utils
{
    void DynamicBuffer::reserve(size_t n)
    {
        if (n > data_.size())
            data_.resize(n);
    }

    size_t DynamicBuffer::size() const noexcept
    {
        return size_;
    }

    size_t DynamicBuffer::capacity() const noexcept
    {
        return data_.size();
    }

    const uint8_t* DynamicBuffer::data() const noexcept
    {
        return data_.data();
    }

    uint8_t* DynamicBuffer::data() noexcept
    {
        return data_.data();
    }

    void DynamicBuffer::clear() noexcept
    {
        size_ = 0;
    }

    uint8_t* DynamicBuffer::reserve_tail(size_t len)
    {
        const size_t need = size_ + len;
        if (need > data_.size())
            grow_(need);
        return data_.data() + size_;
    }

    void DynamicBuffer::commit(size_t n)
    {
        size_ += n;
        if (size_ > data_.size())
            size_ = data_.size();
    }

    void DynamicBuffer::append(const uint8_t* src, size_t len)
    {
        uint8_t* dst = reserve_tail(len);
        std::memcpy(dst, src, len);
        commit(len);
    }

    uint8_t* DynamicBuffer::append_raw(size_t len)
    {
        uint8_t* dst = reserve_tail(len);
        commit(len);
        return dst;
    }

    void DynamicBuffer::shrink(size_t new_size) noexcept
    {
        if (new_size < size_)
            size_ = new_size;
    }

    void DynamicBuffer::grow_(size_t need)
    {
        size_t cap = data_.size() ? data_.size() : 4096;
        while (cap < need)
            cap <<= 1;
        data_.resize(cap);
    }
}