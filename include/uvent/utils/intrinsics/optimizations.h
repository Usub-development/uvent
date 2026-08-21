//
// Created by Kirill Zhukov on 20.04.2025.
//

#ifndef OPTIMIZATIONS_H
#define OPTIMIZATIONS_H

#include <atomic>
#include <cstddef>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386) || defined(_M_IX86)

#include <immintrin.h>

inline void cpu_relax() noexcept { _mm_pause(); }

inline void prefetch_for_write(const void *ptr) noexcept {
    _mm_prefetch(reinterpret_cast<const char *>(ptr), _MM_HINT_T0);
}

inline void prefetch_for_read(const void *ptr) noexcept {
    _mm_prefetch(reinterpret_cast<const char *>(ptr), _MM_HINT_T0);
}

inline void full_fence() noexcept { _mm_mfence(); }

template <class T>
inline void hazard_publish(std::atomic<T*>& slot, T* p) noexcept
{
    slot.exchange(p, std::memory_order_seq_cst);
}

#elif defined(__aarch64__)
inline void cpu_relax() noexcept { asm volatile("yield" ::: "memory"); }

inline void prefetch_for_write(const void* ptr) noexcept
{
    asm volatile("prfm pstl1strm, [%0]" :: "r"(ptr));
}

inline void prefetch_for_read(const void* ptr) noexcept
{
    asm volatile("prfm pldl1keep, [%0]" :: "r"(ptr));
}

inline void full_fence() noexcept { asm volatile("dmb ish" ::: "memory"); }

template <class T>
inline void hazard_publish(std::atomic<T*>& slot, T* p) noexcept
{
    slot.store(p, std::memory_order_relaxed);
    full_fence();
}

#else

inline void cpu_relax() noexcept
{
}

inline void prefetch_for_write(const void*) noexcept
{
}

inline void prefetch_for_read(const void*) noexcept
{
}

inline void full_fence() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }

template <class T>
inline void hazard_publish(std::atomic<T*>& slot, T* p) noexcept
{
    slot.store(p, std::memory_order_seq_cst);
}
#endif

inline void spin_pause_n(std::size_t n) noexcept
{
    while (n--)
        cpu_relax();
}

#endif //OPTIMIZATIONS_H
