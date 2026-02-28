//
// Created by kirill on 1/9/26.
//

#ifndef ASYNCBARRIER_H
#define ASYNCBARRIER_H

#include <atomic>
#include <coroutine>
#include <cstddef>
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync
{
    class AsyncBarrier
    {
    public:
        explicit AsyncBarrier(std::size_t parties) : parties_(parties) {}

        struct Awaiter
        {
            AsyncBarrier& b;

            struct Node
            {
                std::coroutine_handle<> h{};
                Node* next{};
            } node{};

            bool await_ready() const noexcept { return false; }

            bool await_suspend(std::coroutine_handle<> h)
            {
                this->node.h = h;

                this->b.lock_();

                const std::size_t n = ++this->b.arrived_;
                if (n == this->b.parties_)
                {
                    this->b.arrived_ = 0;

                    Node* list = this->b.waiters_;
                    this->b.waiters_ = nullptr;

                    this->b.unlock_();

                    while (list)
                    {
                        Node* next = list->next;
                        system::this_thread::detail::q->enqueue(list->h);
                        list = next;
                    }
                    return false;
                }

                this->node.next = b.waiters_;
                this->b.waiters_ = &node;

                this->b.unlock_();
                return true;
            }

            void await_resume() const noexcept {}
        };

        Awaiter arrive_and_wait() noexcept { return Awaiter{*this}; }

    private:
        void lock_() noexcept
        {
            while (this->spin_.test_and_set(std::memory_order_acquire))
            {
            }
        }

        void unlock_() noexcept { this->spin_.clear(std::memory_order_release); }

        std::size_t parties_{};
        std::size_t arrived_{0};
        std::atomic_flag spin_ = ATOMIC_FLAG_INIT;
        typename Awaiter::Node* waiters_{nullptr};
    };

} // namespace usub::uvent::sync

#endif // ASYNCBARRIER_H
