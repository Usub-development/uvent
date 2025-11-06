//
// Created by kirill on 5/21/25.
//

#ifndef UVENT_DYNAMICBUFFER_H
#define UVENT_DYNAMICBUFFER_H

#pragma once

#include <vector>
#include <cstdint>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace usub::uvent::utils {

    class DynamicBuffer {
    public:
        DynamicBuffer() = default;

        void reserve(size_t n);

        [[nodiscard]] size_t size() const;

        [[nodiscard]] const uint8_t *data() const;

        [[nodiscard]] uint8_t *data();

        void clear();

        void append(const uint8_t *src, size_t len);

        uint8_t *append_raw(size_t len);

        void shrink(size_t new_size);

    private:
        std::vector<uint8_t> data_;
    };

}

#endif //UVENT_DYNAMICBUFFER_H
