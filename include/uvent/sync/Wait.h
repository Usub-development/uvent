#ifndef UVENT_SYNC_WAIT_H
#define UVENT_SYNC_WAIT_H

#include <coroutine>

#include "uvent/sync/SyncCommon.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::sync
{
    template <class Op>
    struct OpWait
    {
        Op* op;
        Waiter node{};
        bool own_cancel{false};

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            auto* f = &uvent::detail::frame_of(h);
            if (f->cancel_requested())
            {
                this->own_cancel = true;
                return false;
            }
            this->node.reset(h, detail::current_thread_id());
            if (!this->op->attach(&this->node))
            {
                if (this->op->detach(&this->node))
                    return false;
                return true;
            }
            if (f->arm_cancel(&OpWait::on_cancel, this, Op::wait_reason()))
            {
                if (this->op->detach(&this->node))
                {
                    f->disarm_cancel();
                    this->own_cancel = true;
                    return false;
                }
            }
            return true;
        }

        bool await_resume() noexcept
        {
            if (this->own_cancel)
                return false;
            if (this->node.h)
                this->op->detach(&this->node);
            return !this->node.cancel_claimed();
        }

        static void on_cancel(uvent::detail::AwaitableFrameBase* f, void* arg) noexcept
        {
            auto* w = static_cast<OpWait*>(arg);
            if (w->op->detach(&w->node))
            {
                w->node.own.store(Waiter::kCancelledIndex, std::memory_order_relaxed);
                system::this_thread::detail::q.enqueue(f->get_coroutine_handle());
            }
        }
    };
} // namespace usub::uvent::sync

#endif // UVENT_SYNC_WAIT_H
