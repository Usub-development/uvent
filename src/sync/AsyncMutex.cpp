#include "uvent/sync/AsyncMutex.h"
#include "uvent/sync/SyncCommon.h"
#include "uvent/system/SystemContext.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::sync
{
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
        this->acquired = this->m->try_lock_raw();
        return this->acquired;
    }

    bool AsyncMutex::LockAwaiter::await_suspend(std::coroutine_handle<> h) noexcept
    {
        auto* f = &uvent::detail::frame_of(h);
        if (f->cancel_requested())
        {
            this->cancelled = true;
            return false;
        }
        this->node.reset(h, detail::current_thread_id());
        this->m->waiters_.lock();
        if (this->m->try_lock_raw())
        {
            this->m->waiters_.unlock();
            this->acquired = true;
            return false;
        }
        this->m->waiters_.push_locked(&this->node);
        this->m->waiters_.unlock();
        if (f->arm_cancel(&LockAwaiter::on_cancel, this, "mutex.lock"))
        {
            if (this->m->waiters_.remove(&this->node))
            {
                f->disarm_cancel();
                this->cancelled = true;
                return false;
            }
        }
        return true;
    }

    AsyncMutex::Guard AsyncMutex::LockAwaiter::await_resume() noexcept
    {
        if (this->acquired)
            return Guard{this->m};
        if (this->cancelled || this->node.cancel_claimed())
            return {};
        return Guard{this->m};
    }

    void AsyncMutex::LockAwaiter::on_cancel(uvent::detail::AwaitableFrameBase* f, void* arg) noexcept
    {
        auto* a = static_cast<LockAwaiter*>(arg);
        if (a->m->waiters_.remove(&a->node))
        {
            a->node.own.store(Waiter::kCancelledIndex, std::memory_order_relaxed);
            system::this_thread::detail::q.enqueue(f->get_coroutine_handle());
        }
    }

    AsyncMutex::LockAwaiter AsyncMutex::lock() noexcept { return LockAwaiter{this}; }

    AsyncMutex::Guard AsyncMutex::try_lock() noexcept
    {
        if (this->try_lock_raw())
            return Guard{this};
        return {};
    }

    void AsyncMutex::unlock() noexcept
    {
        this->waiters_.lock();
        Waiter* w = this->waiters_.pop_front_locked();
        if (!w)
            this->state_.store(0, std::memory_order_release);
        this->waiters_.unlock();
        if (w)
            detail::resume_on(w->h, w->tid);
    }
} // namespace usub::uvent::sync
