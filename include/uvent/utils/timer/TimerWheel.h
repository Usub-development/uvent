//
// Created by kirill on 8/27/24.
//

#ifndef UVENT_TIMER_H
#define UVENT_TIMER_H

// USUB:
#include "include/uvent/system/Defines.h"
#include "include/uvent/tasks/AwaitableFrame.h"
#include "Timer.h"

// STL:
#include <unordered_map>
#include <functional>
#include <coroutine>
#include <iostream>
#include <unistd.h>
#include <cstdint>
#include <chrono>
#include <vector>
#include <mutex>
#include <cmath>
#include <list>
#include <map>
#include "include/uvent/utils/datastructures/queue/ConcurrentQueues.h"
#include "include/uvent/system/Settings.h"

typedef uint64_t timer_duration_t;
typedef uint64_t timeout_t;

namespace usub::uvent::utils
{
    class TimerWheel
    {
    public:
        explicit TimerWheel();

        uint64_t addTimer(Timer* timer);

        bool updateTimer(uint64_t timerId, timer_duration_t new_duration);

        bool removeTimer(uint64_t timerId);

        std::vector<std::coroutine_handle<>> tick();

        int getNextTimeout() const;

        bool empty() const;

    public:
        /**
         * \brief should be locked using lock-free method (e.g try_lock).
         * If it's locked then some thread checks timers.
         * */
        std::mutex mtx;

    private:
        static timeout_t getCurrentTime();

        void addTimerToWheel(Timer* timer, timeout_t expiryTime);

        void removeTimerFromWheel(Timer* timer);

        void advance(std::vector<std::coroutine_handle<>>& timers);

        void updateNextExpiryTime();

        inline static bool is_due(timeout_t now, timeout_t expiry, uint64_t interval) noexcept {
            if (expiry <= now) return true;
            const uint64_t diff = expiry - now;
            return diff < interval;
        }

    private:
        struct Wheel
        {
            Wheel(size_t slots, uint64_t interval)
                : slots_(slots), interval_(interval), currentSlot_(0), minExpiryTime_(0)
            {
                buckets_.resize(slots_);
            }

            size_t slots_;
            uint64_t interval_;
            size_t currentSlot_;
            std::vector<std::list<Timer*>> buckets_;

            timeout_t minExpiryTime_;
        };

        std::vector<Wheel> wheels_;
        timeout_t currentTime_;
        std::unordered_map<uint64_t, Timer*> timerMap_;
        std::atomic<uint64_t> timerIdCounter_{0};
        timeout_t nextExpiryTime_;
        size_t activeTimerCount_;
        queue::concurrent::MPMCQueue<Op> timer_operations_queue;
        std::vector<Op> ops_;
    };
}

#endif //UVENT_TIMER_H
