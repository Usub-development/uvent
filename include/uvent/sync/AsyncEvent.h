#ifndef UVENT_SYNC_ASYNCEVENT_H
#define UVENT_SYNC_ASYNCEVENT_H

#include <atomic>
#include <coroutine>
#include <cstdint>

namespace usub::uvent::sync
{
    enum class Reset
    {
        Auto,
        Manual
    };

    class AsyncEvent
    {
        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
        };

        const Reset reset_;
        std::atomic<bool> set_{false};
        std::atomic<WaitNode*> head_{nullptr};

        static void resume_one(WaitNode* n) noexcept { n->h.resume(); }

    public:
        explicit AsyncEvent(Reset r = Reset::Auto, bool initially_set = false) noexcept :
            reset_(r), set_(initially_set)
        {
        }

        AsyncEvent(const AsyncEvent&) = delete;
        AsyncEvent& operator=(const AsyncEvent&) = delete;
        AsyncEvent(AsyncEvent&&) = delete;
        AsyncEvent& operator=(AsyncEvent&&) = delete;

        struct WaitAwaiter
        {
            AsyncEvent* self;
            WaitNode node{};

            bool await_ready() noexcept
            {
                if (this->self->reset_ == Reset::Auto)
                {
                    bool expected = true;
                    if (this->self->set_.compare_exchange_strong(expected, false, std::memory_order_acquire,
                                                                 std::memory_order_relaxed))
                        return true;
                    return false;
                }
                else
                {
                    return this->self->set_.load(std::memory_order_acquire);
                }
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

                if (this->self->set_.load(std::memory_order_acquire))
                    return false;
                return true;
            }

            void await_resume() noexcept
            {
            }
        };

        WaitAwaiter wait() noexcept { return WaitAwaiter{this}; }

        void set() noexcept
        {
            this->set_.store(true, std::memory_order_release);
            if (this->reset_ == Reset::Auto)
            {
                for (;;)
                {
                    WaitNode* n = this->head_.load(std::memory_order_acquire);
                    if (!n)
                        break;
                    if (this->head_.compare_exchange_weak(n, n->next, std::memory_order_acq_rel,
                                                          std::memory_order_relaxed))
                    {
                        this->set_.store(false, std::memory_order_release);
                        this->resume_one(n);
                        return;
                    }
                }
            }
            else
            {
                WaitNode* list = this->head_.exchange(nullptr, std::memory_order_acq_rel);
                while (list)
                {
                    WaitNode* n = list;
                    list = list->next;
                    this->resume_one(n);
                }
            }
        }

        void reset() noexcept
        {
            if (this->reset_ == Reset::Manual)
                this->set_.store(false, std::memory_order_release);
        }
    };
}

#endif
