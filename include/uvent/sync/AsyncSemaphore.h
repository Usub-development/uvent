#ifndef UVENT_SYNC_ASYNCSEMAPHORE_H
#define UVENT_SYNC_ASYNCSEMAPHORE_H

#include <atomic>
#include <coroutine>
#include <cstdint>

namespace usub::uvent::sync
{
    class AsyncSemaphore
    {
        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
        };

        std::atomic<int32_t> count_;
        std::atomic<WaitNode*> head_{nullptr};

        static void resume_one(WaitNode* n) noexcept { n->h.resume(); }

    public:
        explicit AsyncSemaphore(int32_t initial) noexcept : count_(initial)
        {
        }

        struct AcquireAwaiter
        {
            AsyncSemaphore* self;
            WaitNode node{};

            bool await_ready() noexcept
            {
                int32_t c = this->self->count_.load(std::memory_order_relaxed);
                while (c > 0)
                {
                    if (this->self->count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire,
                                                                 std::memory_order_relaxed))
                        return true;
                }
                return false;
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                this->node.h = h;
                WaitNode* old = this->self->head_.load(std::memory_order_relaxed);
                do
                {
                    this->node.next = old;
                }
                while (!this->self->head_.compare_exchange_weak(old, &this->node, std::memory_order_release,
                                                                std::memory_order_relaxed));

                int32_t c = this->self->count_.load(std::memory_order_acquire);
                while (c > 0)
                {
                    if (this->self->count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire,
                                                                 std::memory_order_relaxed))
                    {
                        WaitNode* cur = this->self->head_.load(std::memory_order_acquire);
                        WaitNode* prev = nullptr;
                        while (cur)
                        {
                            if (cur == &this->node)
                            {
                                WaitNode* nxt = cur->next;
                                if (prev)
                                {
                                    if (prev->next == cur)
                                        prev->next = nxt;
                                }
                                else
                                {
                                    this->self->head_.compare_exchange_strong(
                                        cur, nxt, std::memory_order_acq_rel, std::memory_order_relaxed);
                                }
                                return false;
                            }
                            prev = cur;
                            cur = cur->next;
                        }
                        return false;
                    }
                }
                return true;
            }

            void await_resume() noexcept
            {
            }
        };

        AcquireAwaiter acquire() noexcept { return AcquireAwaiter{this}; }

        bool try_acquire() noexcept
        {
            int32_t c = this->count_.load(std::memory_order_relaxed);
            while (c > 0)
            {
                if (this->count_.compare_exchange_weak(c, c - 1, std::memory_order_acquire, std::memory_order_relaxed))
                    return true;
            }
            return false;
        }

        void release(int32_t k = 1) noexcept
        {
            for (int i = 0; i < k; ++i)
            {
                for (;;)
                {
                    WaitNode* n = this->head_.load(std::memory_order_acquire);
                    if (!n)
                        break;
                    if (this->head_.compare_exchange_weak(n, n->next, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed))
                    {
                        this->resume_one(n);
                        goto next_i;
                    }
                }
                this->count_.fetch_add(1, std::memory_order_release);
            next_i:;
            }
        }
    };
}

#endif