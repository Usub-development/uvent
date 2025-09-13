#ifndef TIMERBENCH_HIGHPERFOMANCETIMER_H
#define TIMERBENCH_HIGHPERFOMANCETIMER_H

#include <chrono>
#include <thread>
#include <cstdint>
#include <iostream>

#if defined(__x86_64__) || defined(_M_X64)

#include <x86intrin.h>

#define USE_RDTSC
#elif defined(__aarch64__)
#define USE_CNTVCT
#else
#define STEADY_CLOCK_FALLBACK
#endif

namespace usub::utils {
    class HighPerfTimer {
    public:
        HighPerfTimer();

        static double calibrate_cpu_ghz();

        void reset();

        [[nodiscard]] uint64_t elapsed_cycles() const;

        [[nodiscard]] uint64_t elapsed_ns() const;

        [[nodiscard]] double elapsed_ms() const;

        [[nodiscard]] double frequency_ghz() const;

    private:
        double cpu_ghz = 0.0;
        uint64_t start_ticks = 0;

#ifdef STEADY_CLOCK_FALLBACK
        std::chrono::steady_clock::time_point chrono_start;
#endif

#ifdef USE_CNTVCT
        static inline uint64_t read_cntvct() {
            uint64_t val;
            asm volatile("mrs %0, cntvct_el0" : "=r"(val));
            return val;
        }

        static inline double get_arm_freq_ghz() {
            uint64_t frq;
            asm volatile("mrs %0, cntfrq_el0" : "=r"(frq));
            return static_cast<double>(frq) / 1e9;
        }
#endif
    };
}

#endif //TIMERBENCH_HIGHPERFOMANCETIMER_H
