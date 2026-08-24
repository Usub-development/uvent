#ifndef UVENT_SYNC_ASYNCSEMAPHORE_H
#define UVENT_SYNC_ASYNCSEMAPHORE_H

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <variant>

#include "uvent/sync/SyncCommon.h"
#include "uvent/sync/Wait.h"
#include "uvent/sync/WaitList.h"

namespace usub::uvent::sync
{

    class AsyncSemaphore
    {
        std::atomic<int32_t> count_;
        WaitList waiters_;

    public:
        explicit AsyncSemaphore(int32_t initial) noexcept : count_(initial) {}

        AsyncSemaphore(const AsyncSemaphore&) = delete;
        AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;

        [[nodiscard]] int32_t available() const noexcept { return this->count_.load(std::memory_order_acquire); }

        bool try_acquire() noexcept
        {
            int32_t c = this->count_.load(std::memory_order_relaxed);
            for (;;)
            {
                if (c <= 0)
                    return false;
                if (this->count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire, std::memory_order_relaxed))
                    return true;
                cpu_relax();
            }
        }

        void release(int32_t k = 1) noexcept
        {
            this->count_.fetch_add(k, std::memory_order_seq_cst);
            detail::notify_fence();
            if (this->waiters_.empty_relaxed())
                return;
            this->waiters_.lock();
            while (k > 0)
            {
                Waiter* w = this->waiters_.pop_front_locked();
                if (!w)
                    break;
                if (detail::fire_waiter(w))
                    --k;
            }
            this->waiters_.unlock();
        }

        struct AcquireOp
        {
            AsyncSemaphore* s;

            using result_type = std::monostate;

            static const char* wait_reason() noexcept { return "semaphore.acquire"; }

            bool try_complete(std::monostate&) const noexcept { return s->try_acquire(); }

            bool attach(Waiter* w) noexcept
            {
                s->waiters_.lock();
                s->waiters_.push_locked(w);
                s->waiters_.unlock();
                detail::notify_fence();
                return s->count_.load(std::memory_order_seq_cst) <= 0;
            }

            bool detach(Waiter* w) noexcept { return s->waiters_.remove(w); }

            void finalize() noexcept {}
        };

        [[nodiscard]] AcquireOp acquire_op() noexcept { return AcquireOp{this}; }

        task::Awaitable<bool> acquire() noexcept
        {
            for (;;)
            {
                AcquireOp op{this};
                std::monostate m;
                if (op.try_complete(m))
                    co_return true;
                OpWait<AcquireOp> w{&op};
                if (!co_await w)
                    co_return false;
            }
        }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_ASYNCSEMAPHORE_H
