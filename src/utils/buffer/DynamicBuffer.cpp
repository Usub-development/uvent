#include "uvent/utils/buffer/DynamicBuffer.h"

#include <cstring>
#include <new>

namespace usub::uvent::utils
{
    static constexpr std::align_val_t kAlign{64};

    void DynamicBuffer::reserve(size_t n)
    {
        if (n > cap_)
            grow_(n);
    }

    uint8_t* DynamicBuffer::reserve_tail(size_t len)
    {
        const size_t need = size_ + len;
        if (need > cap_)
            grow_(need);
        return data_ + size_;
    }

    void DynamicBuffer::commit(size_t n)
    {
        size_ += n;
        if (size_ > cap_)
            size_ = cap_;
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
        size_t cap = cap_ ? cap_ : 4096;
        while (cap < need)
            cap <<= 1;
        auto* p = static_cast<uint8_t*>(::operator new(cap, kAlign));
        if (size_)
            std::memcpy(p, data_, size_);
        if (data_)
            ::operator delete(data_, kAlign);
        data_ = p;
        cap_  = cap;
    }

    void DynamicBuffer::free_() noexcept
    {
        if (data_)
        {
            ::operator delete(data_, kAlign);
            data_ = nullptr;
            cap_  = 0;
            size_ = 0;
        }
    }
}