#ifndef UVENT_SYNC_WAITGROUP_H
#define UVENT_SYNC_WAITGROUP_H

#include <atomic>
#include <coroutine>
#include <variant>

#include "uvent/sync/SyncCommon.h"
#include "uvent/sync/Wait.h"
#include "uvent/sync/WaitList.h"

namespace usub::uvent::sync
{

    class WaitGroup
    {
        std::atomic<int> cnt_{0};
        WaitList waiters_;

    public:
        void add(int n) noexcept { this->cnt_.fetch_add(n, std::memory_order_relaxed); }

        [[nodiscard]] int count() const noexcept { return this->cnt_.load(std::memory_order_acquire); }

        void done() noexcept
        {
            if (this->cnt_.fetch_sub(1, std::memory_order_seq_cst) != 1)
                return;
            detail::notify_fence();
            if (this->waiters_.empty_relaxed())
                return;
            this->waiters_.lock();
            while (Waiter* w = this->waiters_.pop_front_locked())
                detail::fire_waiter(w);
            this->waiters_.unlock();
        }

        struct WaitOp
        {
            WaitGroup* g;

            using result_type = std::monostate;

            static const char* wait_reason() noexcept { return "waitgroup.wait"; }

            bool try_complete(std::monostate&) const noexcept { return g->cnt_.load(std::memory_order_acquire) == 0; }

            bool attach(Waiter* w) noexcept
            {
                g->waiters_.lock();
                g->waiters_.push_locked(w);
                g->waiters_.unlock();
                detail::notify_fence();
                return g->cnt_.load(std::memory_order_seq_cst) != 0;
            }

            bool detach(Waiter* w) noexcept { return g->waiters_.remove(w); }

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

#endif // UVENT_SYNC_WAITGROUP_H
