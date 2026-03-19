#ifndef ASYNCBARRIER_H
#define ASYNCBARRIER_H

#include <atomic>
#include <coroutine>
#include <cstddef>
#include "uvent/sync/SyncCommon.h"
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
                int thread_id{-1};
            } node{};

            bool await_ready() const noexcept { return false; }

            bool await_suspend(std::coroutine_handle<> h)
            {
                node.h = h;
                node.thread_id = detail::current_thread_id();

                b.lock_();

                const std::size_t n = ++b.arrived_;
                if (n == b.parties_)
                {
                    b.arrived_ = 0;

                    Node* list = b.waiters_;
                    b.waiters_ = nullptr;

                    b.unlock_();

                    while (list)
                    {
                        Node* next = list->next;
                        detail::resume_on(list->h, list->thread_id);
                        list = next;
                    }
                    return false;
                }

                node.next = b.waiters_;
                b.waiters_ = &node;

                b.unlock_();
                return true;
            }

            void await_resume() const noexcept {}
        };

        Awaiter arrive_and_wait() noexcept { return Awaiter{*this}; }

    private:
        void lock_() noexcept
        {
            while (spin_.test_and_set(std::memory_order_acquire))
            {
            }
        }

        void unlock_() noexcept { spin_.clear(std::memory_order_release); }

        std::size_t parties_{};
        std::size_t arrived_{0};
        std::atomic_flag spin_ = ATOMIC_FLAG_INIT;
        typename Awaiter::Node* waiters_{nullptr};
    };

} // namespace usub::uvent::sync

#endif // ASYNCBARRIER_H
