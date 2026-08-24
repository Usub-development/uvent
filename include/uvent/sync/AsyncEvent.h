#ifndef UVENT_SYNC_ASYNCEVENT_H
#define UVENT_SYNC_ASYNCEVENT_H

#include <atomic>
#include <coroutine>
#include <variant>

#include "uvent/sync/SyncCommon.h"
#include "uvent/sync/Wait.h"
#include "uvent/sync/WaitList.h"

namespace usub::uvent::sync
{

    enum class Reset
    {
        Auto,
        Manual
    };

    class AsyncEvent
    {
        const Reset reset_;
        std::atomic<bool> set_{false};
        WaitList waiters_;

        void wake_one() noexcept
        {
            if (this->waiters_.empty_relaxed())
                return;
            this->waiters_.lock();
            while (Waiter* w = this->waiters_.pop_front_locked())
                if (detail::fire_waiter(w))
                    break;
            this->waiters_.unlock();
        }

        void wake_all() noexcept
        {
            if (this->waiters_.empty_relaxed())
                return;
            this->waiters_.lock();
            while (Waiter* w = this->waiters_.pop_front_locked())
                detail::fire_waiter(w);
            this->waiters_.unlock();
        }

    public:
        explicit AsyncEvent(Reset r = Reset::Auto, bool initially_set = false) noexcept : reset_(r), set_(initially_set)
        {
        }

        AsyncEvent(const AsyncEvent&) = delete;
        AsyncEvent& operator=(const AsyncEvent&) = delete;
        AsyncEvent(AsyncEvent&&) = delete;
        AsyncEvent& operator=(AsyncEvent&&) = delete;

        [[nodiscard]] bool is_set() const noexcept { return this->set_.load(std::memory_order_acquire); }

        bool try_consume() noexcept
        {
            if (this->reset_ == Reset::Auto)
            {
                bool expected = true;
                return this->set_.compare_exchange_strong(expected, false, std::memory_order_acquire,
                                                          std::memory_order_relaxed);
            }
            return this->is_set();
        }

        void set() noexcept
        {
            if (this->set_.exchange(true, std::memory_order_seq_cst))
                return;
            detail::notify_fence();
            if (this->reset_ == Reset::Auto)
                this->wake_one();
            else
                this->wake_all();
        }

        void reset() noexcept
        {
            if (this->reset_ == Reset::Manual)
                this->set_.store(false, std::memory_order_release);
        }

        struct WaitOp
        {
            AsyncEvent* e;

            using result_type = std::monostate;

            static const char* wait_reason() noexcept { return "event.wait"; }

            bool try_complete(std::monostate&) const noexcept { return e->try_consume(); }

            bool attach(Waiter* w) noexcept
            {
                e->waiters_.lock();
                e->waiters_.push_locked(w);
                e->waiters_.unlock();
                detail::notify_fence();
                return !e->set_.load(std::memory_order_seq_cst);
            }

            bool detach(Waiter* w) noexcept { return e->waiters_.remove(w); }

            void finalize() noexcept {}
        };

        [[nodiscard]] WaitOp wait_op() noexcept { return WaitOp{this}; }

        task::Awaitable<bool> wait() noexcept
        {
            for (;;)
            {
                WaitOp op{this};
                std::monostate m;
                if (op.try_complete(m))
                    co_return true;
                OpWait<WaitOp> w{&op};
                if (!co_await w)
                    co_return false;
            }
        }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_ASYNCEVENT_H
