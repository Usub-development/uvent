//
// Created by kirill on 8/27/24.
//

#include "uvent/utils/timer/TimerWheel.h"

#include <algorithm>

namespace usub::uvent::utils
{
    TimerWheel::TimerWheel()
        : currentTime_(getCurrentTime()), timerIdCounter_(0),
          nextExpiryTime_(0), activeTimerCount_(0)
    {
        /**
         @brief by default used 4 levels:
         LEVEL 0: 256 slots, interval 1 ms
         LEVEL 1: 256 slots, interval 256 ms
         LEVEL 2: 256 slots, interval 65,536 ms
         LEVEL 3: 256 slots, interval 16,777,216 ms
        */
        for (int i = 0; i < settings::tw_levels; i++)
            this->wheels_.emplace_back(256, (1ull << (8 * i)));
        this->ops_.resize(settings::max_pre_allocated_timer_wheel_operations_items);
    }

    uint64_t TimerWheel::addTimer(Timer* timer)
    {
        timer->expiryTime = getCurrentTime() + timer->duration_ms;
#ifndef UVENT_ENABLE_REUSEADDR
        timer->id = timerIdCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
#else
        timer->id = ++this->timerIdCounter_;
#endif

        Op op{ .op = OpType::ADD, .timer = timer };
#ifndef UVENT_ENABLE_REUSEADDR
        while (!this->timer_operations_queue.try_enqueue(op)) cpu_relax();
#else
        this->timer_operations_queue.enqueue(op);
#endif
        return timer->id;
    }

    bool TimerWheel::updateTimer(uint64_t timerId, timer_duration_t new_duration)
    {
        Op op{ .op = OpType::UPDATE, .id = timerId, .new_dur = new_duration };
#ifndef UVENT_ENABLE_REUSEADDR
        while (!this->timer_operations_queue.try_enqueue(op)) cpu_relax();
#else
        this->timer_operations_queue.enqueue(op);
#endif
        return true;
    }

    bool TimerWheel::removeTimer(uint64_t timerId)
    {
        Op op{ .op = OpType::REMOVE, .id_only = timerId };
#ifndef UVENT_ENABLE_REUSEADDR
        while (!this->timer_operations_queue.try_enqueue(op)) cpu_relax();
#else
        this->timer_operations_queue.enqueue(op);
#endif
        return true;
    }

#ifdef UVENT_ENABLE_REUSEADDR
    bool TimerWheel::cancelTimer(uint64_t timerId)
    {
        if (timerId == 0)
            return false;

        auto it = this->timerMap_.find(timerId);
        if (it == this->timerMap_.end())
        {
            this->cancelledPending_.insert(timerId);
            return true;
        }

        Timer* t = it->second;
        if (!t->active)
            return false;

        t->active = false;
        removeTimerFromWheel(t);
        this->timerMap_.erase(it);
        --this->activeTimerCount_;
        if (t->coro)
            t->coro.destroy();
        if (t->heap)
            delete t;
        return true;
    }
#endif

    int TimerWheel::getNextTimeout() const
    {
        const timeout_t now  = getCurrentTime();
        const timeout_t next = this->nextExpiryTime_;

        if (next == 0) return -1;
        if (next <= now) return 0;

        uint64_t diff = next - now;
        if (diff > static_cast<uint64_t>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();

        return static_cast<int>(diff);
    }

    timeout_t TimerWheel::getCurrentTime()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
                   steady_clock::now().time_since_epoch())
            .count();
    }

    void TimerWheel::addTimerToWheel(Timer* timer, timeout_t expiryTime)
    {
        const uint64_t diff = (expiryTime > this->currentTime_)
                                  ? (expiryTime - this->currentTime_)
                                  : 0;

        size_t level = 0;
        while (level + 1 < this->wheels_.size() &&
               diff >= this->wheels_[level].interval_ * this->wheels_[level].slots_)
            ++level;

        Wheel&       wheel = this->wheels_[level];
        const size_t mask  = wheel.slots_ - 1;

        const size_t slot = (diff == 0)
                                ? ((this->currentTime_ + 1) & mask)
                                : ((expiryTime / wheel.interval_) & mask);

        timer->level       = level;
        timer->slotIndex   = slot;
        timer->posInBucket = wheel.buckets_[slot].size();
        wheel.buckets_[slot].push_back(timer);

        if (this->nextExpiryTime_ == 0 || expiryTime < this->nextExpiryTime_)
            this->nextExpiryTime_ = expiryTime;
    }

    void TimerWheel::removeTimerFromWheel(Timer* timer)
    {
        if (timer->level >= this->wheels_.size())
            return;

        Wheel& wheel  = this->wheels_[timer->level];
        auto&  bucket = wheel.buckets_[timer->slotIndex];

        const size_t pos = timer->posInBucket;
        if (pos < bucket.size() && bucket[pos] == timer)
        {
            const size_t last = bucket.size() - 1;
            if (pos != last)
            {
                bucket[pos]              = bucket[last];
                bucket[pos]->posInBucket = pos;
            }
            bucket.pop_back();
        }
        else
        {
            auto it = std::find(bucket.begin(), bucket.end(), timer);
            if (it != bucket.end())
            {
                const size_t idx  = static_cast<size_t>(it - bucket.begin());
                const size_t last = bucket.size() - 1;
                if (idx != last)
                {
                    bucket[idx]              = bucket[last];
                    bucket[idx]->posInBucket = idx;
                }
                bucket.pop_back();
            }
        }

        if (timer->expiryTime == this->nextExpiryTime_)
            this->nextExpiryDirty_ = true;
    }

    void TimerWheel::updateNextExpiryTime()
    {
        this->nextExpiryTime_ = 0;
        for (const auto& wheel : this->wheels_)
        {
            for (const auto& bucket : wheel.buckets_)
            {
                for (const auto* t : bucket)
                {
                    if (!t->active) continue;
                    if (this->nextExpiryTime_ == 0 || t->expiryTime < this->nextExpiryTime_)
                        this->nextExpiryTime_ = t->expiryTime;
                }
            }
        }
    }

    void TimerWheel::refreshNextExpiry()
    {
        /**
         * @brief Keeps nextExpiryTime_ consistent after a tick.
         *
         * The minimum is recomputed at most once per tick, batching every
         * add/update/remove/fire that happened during op draining and advancing.
         * A full rescan only runs when the earliest timer was disturbed
         * (nextExpiryDirty_) or the value is unset (== 0); pure additions keep the
         * incremental min and stay O(1).
         */
        if (this->activeTimerCount_ == 0)
            this->nextExpiryTime_ = 0;
        else if (this->nextExpiryDirty_ || this->nextExpiryTime_ == 0)
            updateNextExpiryTime();

        this->nextExpiryDirty_ = false;
    }

    void TimerWheel::tick()
    {
        for (;;)
        {
            const size_t cap = this->ops_.size();
#ifndef UVENT_ENABLE_REUSEADDR
            size_t n = this->timer_operations_queue.try_dequeue_bulk(
                this->ops_.data(), cap);
#else
            const size_t n = this->timer_operations_queue.dequeue_bulk(
                this->ops_.data(), cap);
#endif
            if (n == 0) break;

            for (size_t i = 0; i < n; ++i)
            {
                auto& op = this->ops_[i];
                switch (op.op)
                {
                case OpType::ADD:
                {
                    Timer* t = op.timer;
#ifdef UVENT_ENABLE_REUSEADDR
                    if (!this->cancelledPending_.empty() && this->cancelledPending_.erase(t->id))
                    {
                        if (t->coro)
                            t->coro.destroy();
                        if (t->heap)
                            delete t;
                        break;
                    }
#endif
                    addTimerToWheel(t, t->expiryTime);
                    this->timerMap_[t->id] = t;
                    ++this->activeTimerCount_;
                    break;
                }

                case OpType::UPDATE:
                {
                    auto it = timerMap_.find(op.id);
                    if (it != timerMap_.end())
                    {
                        Timer* t = it->second;
                        if (t->active)
                        {
                            removeTimerFromWheel(t);
                            t->duration_ms = op.new_dur;
                            t->expiryTime  = getCurrentTime() + t->duration_ms;
                            addTimerToWheel(t, t->expiryTime);
                        }
                    }
                    break;
                }

                case OpType::REMOVE:
                {
                    auto it = timerMap_.find(op.id_only);
                    if (it != this->timerMap_.end())
                    {
                        Timer* t = it->second;
                        if (t->active)
                        {
                            t->active = false;
                            removeTimerFromWheel(t);
                            this->timerMap_.erase(it);
                            --this->activeTimerCount_;
                            if (t->coro) t->coro.destroy();
                            if (t->heap) delete t;
                        }
                    }
                    break;
                }
                }
            }
        }

        const timeout_t newTime = getCurrentTime();

        if (newTime <= this->currentTime_)
        {
            refreshNextExpiry();
            return;
        }

        while (this->currentTime_ < newTime)
            advance();

        refreshNextExpiry();

        if (!this->fired_raw_.empty())
        {
            for (auto& [fn, arg] : this->fired_raw_)
                fn(arg);
            this->fired_raw_.clear();
        }
    }

    void TimerWheel::advance()
    {
        ++this->currentTime_;

        for (size_t level = this->wheels_.size(); level-- > 1;)
        {
            const Wheel& wheel = this->wheels_[level];
            if (this->currentTime_ % wheel.interval_ != 0)
                continue;
            processSlot(level, (this->currentTime_ / wheel.interval_) & (wheel.slots_ - 1));
        }

        const Wheel& l0 = this->wheels_[0];
        processSlot(0, this->currentTime_ & (l0.slots_ - 1));
    }

    void TimerWheel::processSlot(size_t level, size_t slot)
    {
        auto& bucket = this->wheels_[level].buckets_[slot];

        auto swap_pop_at = [&](size_t at)
        {
            const size_t last = bucket.size() - 1;
            if (at != last)
            {
                bucket[at]              = bucket[last];
                bucket[at]->posInBucket = at;
            }
            bucket.pop_back();
        };

        size_t i = 0;
        while (i < bucket.size())
        {
            Timer* timer = bucket[i];

            if (timer->active)
            {
                if (timer->expiryTime <= this->currentTime_)
                {
                    if (timer->raw_fn)
                        this->fired_raw_.emplace_back(timer->raw_fn, timer->raw_arg);
                    else if (timer->coro)
                        system::this_thread::detail::q.enqueue(timer->coro);

                    timer->active = false;
                    this->timerMap_.erase(timer->id);
                    --this->activeTimerCount_;

                    if (timer->expiryTime == this->nextExpiryTime_)
                        this->nextExpiryDirty_ = true;

                    if (timer->heap)
                        delete timer;
                }
                else
                {
                    addTimerToWheel(timer, timer->expiryTime);
                }
            }

            swap_pop_at(i);
        }
    }

    bool TimerWheel::empty() const
    {
        return this->activeTimerCount_ == 0;
    }

    task::Awaitable<void> timeout_coroutine(std::function<void(std::any&)> f, std::any arg)
    {
        f(arg);
        co_return;
    }
}
