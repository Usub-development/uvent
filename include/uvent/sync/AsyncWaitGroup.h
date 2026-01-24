#ifndef UVENT_SYNC_WAITGROUP_H
#define UVENT_SYNC_WAITGROUP_H

#include <atomic>
#include <coroutine>
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync
{
    class WaitGroup
    {
        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
        };

        std::atomic<int> cnt_{0};
        std::atomic<WaitNode*> head_{nullptr};

        static void resume_one(WaitNode* n) noexcept
        {
            system::co_spawn_static(n->h,
                                    std::coroutine_handle<detail::AwaitableFrameBase>::from_address(n->h.address())
                                        .promise()
                                        .get_thread_id());
        }

    public:
        void add(int n) noexcept { this->cnt_.fetch_add(n, std::memory_order_relaxed); }

        void done() noexcept
        {
            int v = this->cnt_.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (v == 0)
            {
                WaitNode* list = this->head_.exchange(nullptr, std::memory_order_acq_rel);
                while (list)
                {
                    auto* n = list;
                    list = list->next;
                    this->resume_one(n);
                }
            }
        }

        struct Awaiter
        {
            WaitGroup* self;
            WaitNode node{};
            bool await_ready() noexcept { return this->self->cnt_.load(std::memory_order_acquire) == 0; }

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
                if (this->self->cnt_.load(std::memory_order_acquire) == 0)
                    return false;
                return true;
            }

            void await_resume() noexcept {}
        };

        Awaiter wait() noexcept { return Awaiter{this}; }
    };
} // namespace usub::uvent::sync

#endif
