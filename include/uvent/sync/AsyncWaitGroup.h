#ifndef UVENT_SYNC_WAITGROUP_H
#define UVENT_SYNC_WAITGROUP_H

#include <atomic>
#include <coroutine>

#include "uvent/sync/AsyncSemaphore.h"

namespace usub::uvent::sync
{

    class WaitGroup
    {
        std::atomic<int> cnt_{0};
        AsyncSemaphore sem_{0};

    public:
        void add(int n) noexcept { cnt_.fetch_add(n, std::memory_order_relaxed); }

        void done() noexcept
        {
            int v = cnt_.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (v == 0)
                sem_.release(1);
        }

        struct Awaiter
        {
            WaitGroup* self;

            bool await_ready() noexcept { return self->cnt_.load(std::memory_order_acquire) == 0; }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                if (self->cnt_.load(std::memory_order_acquire) == 0)
                    return false;
                auto a = self->sem_.acquire();
                return a.await_suspend(h);
            }

            void await_resume() noexcept {}
        };

        Awaiter wait() noexcept { return Awaiter{this}; }
    };

} // namespace usub::uvent::sync

#endif // UVENT_SYNC_WAITGROUP_H
