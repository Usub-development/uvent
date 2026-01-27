#ifndef UVENT_SYNC_ASYNCSEMAPHORE_H
#define UVENT_SYNC_ASYNCSEMAPHORE_H

#include <atomic>
#include <coroutine>
#include <cstdint>

#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync
{
    class AsyncSemaphore
    {
        enum class NodeState : uint8_t
        {
            Waiting = 0,
            Cancelled = 1,
            Claimed = 2
        };

        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
            int thread_id{};
            std::atomic<NodeState> st{NodeState::Waiting};
        };

        std::atomic<int32_t> count_;
        std::atomic<WaitNode*> head_{nullptr};

        static int get_thread_id(std::coroutine_handle<> h) noexcept
        {
            return std::coroutine_handle<detail::AwaitableFrameBase>::from_address(h.address())
                .promise()
                .get_thread_id();
        }

        void push_waiter(WaitNode* n) noexcept
        {
            WaitNode* old = this->head_.load(std::memory_order_relaxed);
            do
            {
                n->next = old;
            }
            while (!this->head_.compare_exchange_weak(old, n, std::memory_order_release, std::memory_order_relaxed));
        }

        WaitNode* pop_waiter() noexcept
        {
            WaitNode* n = this->head_.load(std::memory_order_acquire);
            while (n)
            {
                WaitNode* next = n->next;
                if (this->head_.compare_exchange_weak(n, next, std::memory_order_acq_rel, std::memory_order_relaxed))
                    return n;
            }
            return nullptr;
        }

        bool try_take_token() noexcept
        {
            int32_t c = this->count_.load(std::memory_order_relaxed);
            while (c > 0)
            {
                if (this->count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire, std::memory_order_relaxed))
                    return true;
            }
            return false;
        }

    public:
        explicit AsyncSemaphore(int32_t initial) noexcept : count_(initial) {}

        struct AcquireAwaiter
        {
            AsyncSemaphore* self{};
            WaitNode node{};

            bool await_ready() noexcept { return self->try_take_token(); }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                this->node.h = h;
                this->node.thread_id = get_thread_id(h);
                this->node.st.store(NodeState::Waiting, std::memory_order_relaxed);

                this->self->push_waiter(&this->node);

                if (this->self->try_take_token())
                {
                    NodeState expected = NodeState::Waiting;
                    if (node.st.compare_exchange_strong(expected, NodeState::Cancelled, std::memory_order_acq_rel,
                                                        std::memory_order_relaxed))
                        return false;
                    return true;
                }

                return true;
            }

            void await_resume() noexcept {}
        };

        AcquireAwaiter acquire() noexcept { return AcquireAwaiter{this}; }

        bool try_acquire() noexcept { return try_take_token(); }

        void release(int32_t k = 1) noexcept
        {
            for (int32_t i = 0; i < k; ++i)
            {
                for (;;)
                {
                    WaitNode* n = pop_waiter();
                    if (!n)
                    {
                        this->count_.fetch_add(1, std::memory_order_release);
                        break;
                    }

                    NodeState expected = NodeState::Waiting;
                    if (!n->st.compare_exchange_strong(expected, NodeState::Claimed, std::memory_order_acq_rel,
                                                       std::memory_order_relaxed))
                        continue;

                    system::co_spawn_static(n->h, n->thread_id);
                    break;
                }
            }
        }
    };
} // namespace usub::uvent::sync

#endif
