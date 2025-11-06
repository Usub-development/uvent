//
// Created by kirill on 5/21/25.
//

#include "uvent/utils/buffer/DynamicBuffer.h"

namespace usub::uvent::utils {
    void DynamicBuffer::reserve(size_t n) {
        this->data_.reserve(n);
    }

    size_t DynamicBuffer::size() const {
        return this->data_.size();
    }

    const uint8_t *DynamicBuffer::data() const {
        return this->data_.data();
    }

    uint8_t* DynamicBuffer::data()
    {
        return this->data_.data();
    }

    void DynamicBuffer::clear() {
        this->data_.clear();
    }

    void DynamicBuffer::append(const uint8_t *src, size_t len) {
        size_t old_size = this->data_.size();
        this->data_.resize(old_size + len);
        uint8_t *dst = this->data_.data() + old_size;

#if defined(__AVX2__)
        size_t i = 0;
        for (; i + 32 <= len; i += 32) {
            __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), chunk);
        }
        for (; i < len; ++i) dst[i] = src[i];
#elif defined(__aarch64__)
        size_t i = 0;
        for (; i + 16 <= len; i += 16) {
            asm volatile(
                "ld1 {v0.16b}, [%[src]]\n"
                "st1 {v0.16b}, [%[dst]]\n"
                :
                : [src] "r"(src + i), [dst] "r"(dst + i)
                : "v0", "memory"
            );
        }
        for (; i < len; ++i) dst[i] = src[i];
#else
        std::memcpy(dst, src, len);
#endif
    }

    uint8_t *DynamicBuffer::append_raw(size_t len) {
        size_t old_size = this->data_.size();
        this->data_.resize(old_size + len);
        return this->data_.data() + old_size;
    }

    void DynamicBuffer::shrink(size_t new_size) {
        if (new_size < this->data_.size())
            this->data_.resize(new_size);
    }

}