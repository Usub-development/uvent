#include "uvent/sync/AsyncMutex.h"
#include "uvent/system/SystemContext.h"

namespace usub::uvent::sync
{
    AsyncMutex::WaitNode* AsyncMutex::ptr_untag(std::uintptr_t s) noexcept
    {
        return reinterpret_cast<WaitNode*>(s & ~TAG);
    }

    std::uintptr_t AsyncMutex::ptr_tag(WaitNode* p) noexcept { return reinterpret_cast<std::uintptr_t>(p) | TAG; }

    AsyncMutex::Guard::Guard(AsyncMutex* m) noexcept : m_(m) {}

    AsyncMutex::Guard::Guard(Guard&& o) noexcept : m_(o.m_) { o.m_ = nullptr; }

    AsyncMutex::Guard& AsyncMutex::Guard::operator=(Guard&& o) noexcept
    {
        if (this != &o)
        {
            if (this->m_)
                this->m_->unlock();
            this->m_ = o.m_;
            o.m_ = nullptr;
        }
        return *this;
    }

    AsyncMutex::Guard::~Guard()
    {
        if (this->m_)
            this->m_->unlock();
    }

    bool AsyncMutex::Guard::owns_lock() const noexcept { return this->m_ != nullptr; }

    void AsyncMutex::Guard::unlock() noexcept
    {
        if (this->m_)
        {
            auto* t = this->m_;
            this->m_ = nullptr;
            t->unlock();
        }
    }

    bool AsyncMutex::LockAwaiter::await_ready() noexcept
    {
        auto exp = kUnlocked;
        return this->m->state_.compare_exchange_strong(exp, kLockedNoWaiters, std::memory_order_acquire,
                                                       std::memory_order_relaxed);
    }

    bool AsyncMutex::LockAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
    {
        this->node.h = h;
        for (;;)
        {
            auto s = this->m->state_.load(std::memory_order_acquire);
            if (s == kUnlocked)
            {
                if (this->m->state_.compare_exchange_weak(s, kLockedNoWaiters, std::memory_order_acquire,
                                                          std::memory_order_acquire))
                    return false;
                continue;
            }

            WaitNode* head = this->m->ptr_untag(s);
            this->node.next = head;
            const auto new_state = this->m->ptr_tag(&this->node);
            if (this->m->state_.compare_exchange_weak(s, new_state, std::memory_order_release,
                                                      std::memory_order_acquire))
                return true;
        }
    }

    AsyncMutex::Guard AsyncMutex::LockAwaiter::await_resume() noexcept { return Guard{this->m}; }

    AsyncMutex::LockAwaiter AsyncMutex::lock() noexcept { return LockAwaiter{this}; }

    AsyncMutex::Guard AsyncMutex::try_lock() noexcept
    {
        auto exp = kUnlocked;
        if (this->state_.compare_exchange_strong(exp, kLockedNoWaiters, std::memory_order_acquire,
                                                 std::memory_order_relaxed))
            return Guard{this};
        return {};
    }

    void AsyncMutex::unlock() noexcept
    {
        for (;;)
        {
            auto s = this->state_.load(std::memory_order_acquire);
            if (s == kLockedNoWaiters)
            {
                if (this->state_.compare_exchange_weak(s, kUnlocked, std::memory_order_release,
                                                       std::memory_order_relaxed))
                    return;
                continue;
            }

            WaitNode* head = this->ptr_untag(s);
            WaitNode* next = head->next;
            const std::uintptr_t new_state = next ? this->ptr_tag(next) : kLockedNoWaiters;
            if (this->state_.compare_exchange_weak(s, new_state, std::memory_order_acquire, std::memory_order_acquire))
            {
                system::this_thread::detail::q->enqueue(head->h);
                return;
            }
        }
    }
} // namespace usub::uvent::sync
