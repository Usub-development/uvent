#ifndef UVENT_SYNC_WAITLIST_H
#define UVENT_SYNC_WAITLIST_H

#include <atomic>
#include <coroutine>
#include <cstdint>

#include "uvent/utils/intrinsics/optimizations.h"

namespace usub::uvent::sync
{
    struct Waiter
    {
        static constexpr int32_t kCancelledIndex = -2;

        std::coroutine_handle<> h{};
        std::atomic<int32_t>* winner{nullptr};
        std::atomic<int32_t> own{-1};
        Waiter* prev{nullptr};
        Waiter* next{nullptr};
        int32_t index{0};
        int tid{-1};
        bool in_list{false};

        void reset(std::coroutine_handle<> handle, int thread_id) noexcept
        {
            this->h = handle;
            this->tid = thread_id;
            this->winner = &this->own;
            this->own.store(-1, std::memory_order_relaxed);
            this->index = 0;
        }

        bool claim(int32_t idx) noexcept
        {
            int32_t expected = -1;
            return this->winner->compare_exchange_strong(expected, idx, std::memory_order_acq_rel,
                                                         std::memory_order_relaxed);
        }

        bool cancel_claimed() const noexcept { return this->own.load(std::memory_order_relaxed) == kCancelledIndex; }
    };

    class WaitList
    {
    public:
        void lock() noexcept
        {
            while (this->lock_.test_and_set(std::memory_order_acquire))
                cpu_relax();
        }

        void unlock() noexcept { this->lock_.clear(std::memory_order_release); }

        [[nodiscard]] bool empty_relaxed() const noexcept { return this->count_.load(std::memory_order_relaxed) == 0; }

        [[nodiscard]] std::uint32_t size_relaxed() const noexcept
        {
            return this->count_.load(std::memory_order_relaxed);
        }

        void push_locked(Waiter* w) noexcept
        {
            w->prev = this->tail_;
            w->next = nullptr;
            if (this->tail_)
                this->tail_->next = w;
            else
                this->head_ = w;
            this->tail_ = w;
            w->in_list = true;
            this->count_.fetch_add(1, std::memory_order_seq_cst);
        }

        Waiter* pop_front_locked() noexcept
        {
            Waiter* w = this->head_;
            if (!w)
                return nullptr;
            this->head_ = w->next;
            if (this->head_)
                this->head_->prev = nullptr;
            else
                this->tail_ = nullptr;
            w->prev = nullptr;
            w->next = nullptr;
            w->in_list = false;
            this->count_.fetch_sub(1, std::memory_order_relaxed);
            return w;
        }

        bool remove_locked(Waiter* w) noexcept
        {
            if (!w->in_list)
                return false;
            if (w->prev)
                w->prev->next = w->next;
            else
                this->head_ = w->next;
            if (w->next)
                w->next->prev = w->prev;
            else
                this->tail_ = w->prev;
            w->prev = nullptr;
            w->next = nullptr;
            w->in_list = false;
            this->count_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }

        bool remove(Waiter* w) noexcept
        {
            this->lock();
            const bool r = this->remove_locked(w);
            this->unlock();
            return r;
        }

    private:
        std::atomic_flag lock_{};
        std::atomic<std::uint32_t> count_{0};
        Waiter* head_{nullptr};
        Waiter* tail_{nullptr};
    };

    namespace detail
    {
        inline void notify_fence() noexcept { std::atomic_thread_fence(std::memory_order_seq_cst); }
    } // namespace detail
} // namespace usub::uvent::sync

#endif // UVENT_SYNC_WAITLIST_H
