#ifndef UVENT_ASYNC_MUTEX_H
#define UVENT_ASYNC_MUTEX_H

#include <atomic>
#include <coroutine>
#include <cstddef>
#include <cstdint>

namespace usub::uvent::sync
{
    class AsyncMutex
    {
        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
        };

        std::atomic<std::uintptr_t> state_{0};

        static constexpr std::uintptr_t kUnlocked = 0;
        static constexpr std::uintptr_t kLockedNoWaiters = 1;
        static constexpr std::uintptr_t TAG = 1;

        static WaitNode* ptr_untag(std::uintptr_t s) noexcept;
        static std::uintptr_t ptr_tag(WaitNode* p) noexcept;

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
            WaitNode node{};
            bool await_ready() noexcept;
            bool await_suspend(std::coroutine_handle<> h) noexcept;
            Guard await_resume() noexcept;
        };

        LockAwaiter lock() noexcept;
        Guard try_lock() noexcept;
        void unlock() noexcept;
    };
} // namespace usub::uvent::sync

#endif
