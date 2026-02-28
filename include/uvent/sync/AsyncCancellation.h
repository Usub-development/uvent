#ifndef UVENT_SYNC_CANCELLATION_H
#define UVENT_SYNC_CANCELLATION_H

#include <atomic>
#include <coroutine>
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync
{
    struct CancelState
    {
        std::atomic<bool> requested{false};

        struct WaitNode
        {
            std::coroutine_handle<> h{};
            WaitNode* next{};
        };

        std::atomic<WaitNode*> head{nullptr};
    };

    class CancellationToken
    {
        CancelState* s_{};

    public:
        explicit CancellationToken(CancelState* s = nullptr) noexcept : s_(s) {}

        bool stop_requested() const noexcept { return this->s_ && this->s_->requested.load(std::memory_order_acquire); }

        struct Awaiter
        {
            CancelState* s;
            CancelState::WaitNode node{};

            bool await_ready() noexcept { return this->s->requested.load(std::memory_order_acquire); }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                this->node.h = h;
                auto* old = this->s->head.load(std::memory_order_relaxed);
                do
                {
                    this->node.next = old;
                }
                while (!this->s->head.compare_exchange_weak(old, &this->node, std::memory_order_release,
                                                            std::memory_order_relaxed));
                if (this->s->requested.load(std::memory_order_acquire))
                    return false;
                return true;
            }

            void await_resume() noexcept {}
        };

        Awaiter on_cancel() const noexcept { return Awaiter{this->s_}; }
    };

    class CancellationSource
    {
        CancelState state_;

    public:
        CancellationToken token() noexcept { return CancellationToken(&this->state_); }

        void request_cancel() noexcept
        {
            bool was = this->state_.requested.exchange(true, std::memory_order_acq_rel);
            if (was)
                return;
            auto* list = this->state_.head.exchange(nullptr, std::memory_order_acq_rel);
            while (list)
            {
                auto* n = list;
                list = list->next;
                system::this_thread::detail::q->enqueue(n->h);
            }
        }
    };
} // namespace usub::uvent::sync

#endif
