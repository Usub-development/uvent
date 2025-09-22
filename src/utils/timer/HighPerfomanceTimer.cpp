//
// Created by kirill on 5/7/25.
//

#include "uvent/utils/timer/HighPerfomanceTimer.h"

namespace usub::utils {
    HighPerfTimer::HighPerfTimer() {
#ifdef USE_RDTSC
        uint32_t aux;
        this->cpu_ghz = calibrate_cpu_ghz();
        this->start_ticks = __rdtscp(&aux);
#elif defined(USE_CNTVCT)
        cpu_ghz = get_arm_freq_ghz();
            start_ticks = read_cntvct();
#elif defined(STEADY_CLOCK_FALLBACK)
            chrono_start = std::chrono::steady_clock::now();
#endif
    }

    double HighPerfTimer::calibrate_cpu_ghz() {
        using namespace std::chrono;
        constexpr int sleep_ms = 100;

#ifdef USE_RDTSC
        uint32_t aux;
        auto start = high_resolution_clock::now();
        uint64_t t0 = __rdtscp(&aux);
        std::this_thread::sleep_for(milliseconds(sleep_ms));
        uint64_t t1 = __rdtscp(&aux);
        auto end = high_resolution_clock::now();
        double elapsed = duration<double>(end - start).count();
        return static_cast<double>(t1 - t0) / 1e9 / elapsed;
#elif defined(USE_CNTVCT)
        return get_arm_freq_ghz();
#else
        return 1.0; // dummy value to avoid division by zero
#endif
    }

    void HighPerfTimer::reset() {
#ifdef USE_RDTSC
        uint32_t aux;
        this->start_ticks = __rdtscp(&aux);
#elif defined(USE_CNTVCT)
        start_ticks = read_cntvct();
#elif defined(STEADY_CLOCK_FALLBACK)
            chrono_start = std::chrono::steady_clock::now();
#endif
    }

    uint64_t HighPerfTimer::elapsed_cycles() const {
#ifdef USE_RDTSC
        uint32_t aux;
        return __rdtscp(&aux) - this->start_ticks;
#elif defined(USE_CNTVCT)
        return read_cntvct() - this->start_ticks;
#else
            return 0; // not used
#endif
    }

    uint64_t HighPerfTimer::elapsed_ns() const {
#ifdef USE_RDTSC
        return static_cast<uint64_t>(static_cast<double>(elapsed_cycles()) / this->cpu_ghz);
#elif defined(USE_CNTVCT)
        return static_cast<uint64_t>(elapsed_cycles() / cpu_ghz);
#elif defined(STEADY_CLOCK_FALLBACK)
            auto now = std::chrono::steady_clock::now();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(now - chrono_start).count();
#endif
    }

    double HighPerfTimer::elapsed_ms() const {
        return static_cast<double>(elapsed_ns()) / 1e6;
    }

    double HighPerfTimer::frequency_ghz() const {
        return this->cpu_ghz;
    }
}
