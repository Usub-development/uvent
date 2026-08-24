//
// Created by kirill on 8/27/24.
//

#ifndef UVENT_TIMER_H
#define UVENT_TIMER_H

#include "Timer.h"
#include "uvent/system/Defines.h"
#include "uvent/tasks/AwaitableFrame.h"

#include <coroutine>
#include <functional>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#endif
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>
#include "uvent/system/Settings.h"
#include "uvent/utils/datastructures/queue/ConcurrentQueues.h"

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

        /**
         * Queue a REMOVE for `timerId`. If `done` is given it is invoked by the wheel
         * (on the ticking thread) once the timer node is guaranteed unreferenced by
         * the wheel — the only safe moment to free an object with an embedded Timer.
         */
        bool removeTimer(uint64_t timerId, raw_timer_fn done = nullptr, void* done_arg = nullptr);

        /**
         * Synchronous cancel: drops the node from the wheel right now (or marks a
         * still-queued ADD to be dropped). Returns true iff the timer had not fired
         * yet (the caller then owns whatever reference the timer held).
         * REUSEADDR: wheel is thread_local — call on the owning worker only.
         * Non-REUSEADDR: the wheel is shared — the caller must hold `mtx`
         * (see cancelTimerSync); never call from inside a timer callback (tick holds mtx).
         */
        bool cancelTimer(uint64_t timerId);

        /**
         * Synchronous cancel that transfers ownership to the caller: drops the node
         * from the wheel (or marks a queued ADD to be dropped) WITHOUT destroying a
         * bound coroutine and WITHOUT deleting a heap timer. Returns true iff the
         * wheel will no longer touch the timer -- the caller then owns the Timer
         * object and any coroutine bound to it.
         */
        bool cancelTimerDetach(uint64_t timerId);
#ifndef UVENT_ENABLE_REUSEADDR
        bool cancelTimerSync(uint64_t timerId)
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            return cancelTimer(timerId);
        }

        bool cancelTimerSyncDetach(uint64_t timerId)
        {
            std::lock_guard<std::mutex> lk(this->mtx);
            return cancelTimerDetach(timerId);
        }
#endif

        void tick();

        int getNextTimeout() const;

        bool empty() const;

    public:
#ifndef UVENT_ENABLE_REUSEADDR
        std::mutex mtx;
#endif

    private:
        static timeout_t getCurrentTime();

        void addTimerToWheel(Timer* timer, timeout_t expiryTime);

        void removeTimerFromWheel(Timer* timer);

        void advance();

        void processSlot(size_t level, size_t slot);

        void updateNextExpiryTime();

        void refreshNextExpiry();

    private:
        struct Wheel
        {
            Wheel(size_t slots, uint64_t interval) : slots_(slots), interval_(interval)
            {
                buckets_.resize(slots_);
                for (auto& b : buckets_)
                    b.reserve(2);
            }

            size_t slots_;
            uint64_t interval_;
            std::vector<std::vector<Timer*>> buckets_;
        };

        std::vector<Wheel> wheels_;
        timeout_t currentTime_;
        std::unordered_map<uint64_t, Timer*> timerMap_;
#ifndef UVENT_ENABLE_REUSEADDR
        std::atomic<uint64_t> timerIdCounter_{0};
#else
        uint64_t timerIdCounter_{0};
#endif
        timeout_t nextExpiryTime_;
        bool nextExpiryDirty_{false};
        size_t activeTimerCount_;
#ifndef UVENT_ENABLE_REUSEADDR
        queue::concurrent::SegmentedMPMCQueue<Op> timer_operations_queue;
#else
        queue::single_thread::Queue<Op> timer_operations_queue;
#endif
        std::vector<Op> ops_;
        std::vector<std::pair<raw_timer_fn, void*>> fired_raw_;
        /// ids whose cancel/remove arrived before their ADD was drained; the ADD is dropped
        std::unordered_set<uint64_t> cancelledPending_;
        /// ids detached (cancelTimerDetach) before their ADD was drained; the ADD is
        /// dropped without touching the Timer -- ownership already went to the caller
        std::unordered_set<uint64_t> detachedPending_;
        /// largest id whose ADD has been drained (ids are monotonic per wheel): a
        /// cancel/remove for id <= this cannot have a queued ADD -> nothing to remember
        uint64_t maxAddedId_{0};
#ifdef UVENT_ENABLE_REUSEADDR
#endif
    };
} // namespace usub::uvent::utils

#endif // UVENT_TIMER_H
