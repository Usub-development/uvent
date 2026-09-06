#ifndef UVENT_ASYNC_MUTEX_H
#define UVENT_ASYNC_MUTEX_H

#include <atomic>
#include <coroutine>
#include <cstdint>

#include "uvent/sync/WaitList.h"

namespace usub::uvent::detail
{
    class AwaitableFrameBase;
}

namespace usub::uvent::sync
{

    class AsyncMutex
    {
        std::atomic<uint32_t> state_{0};
        WaitList waiters_;

        bool try_lock_raw() noexcept
        {
            uint32_t expected = 0;
            return this->state_.compare_exchange_strong(expected, 1, std::memory_order_acquire,
                                                        std::memory_order_relaxed);
        }

    public:
        class Guard
        {
            AsyncMutex* m_{};

        public:
            Guard() = default;
            explicit Guard(AsyncMutex* m) noexcept;
            Guard(Guard&& o) noexcept;
            Guard& operator=(Guard&& o) noexcept;
            ~Guard();
            bool owns_lock() const noexcept;
            void unlock() noexcept;
        };

        struct LockAwaiter
        {
            AsyncMutex* m;
            Waiter node{};
            bool acquired{false};
            bool cancelled{false};

            bool await_ready() noexcept;
            bool await_suspend(std::coroutine_handle<> h) noexcept;
            Guard await_resume() noexcept;

            static void on_cancel(uvent::detail::AwaitableFrameBase* f, void* arg) noexcept;
        };

        LockAwaiter lock() noexcept;
        Guard try_lock() noexcept;

        void unlock() noexcept;
    };

} // namespace usub::uvent::sync

#endif // UVENT_ASYNC_MUTEX_H
