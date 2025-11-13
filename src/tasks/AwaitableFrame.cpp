//
// Created by kirill on 1/4/25.
//

#include "uvent/tasks/AwaitableFrame.h"
#include "uvent/system/SystemContext.h"

namespace usub::uvent::detail
{
    void AwaitableFrameBase::destroy(DestroyingPolicy policy)
    {
        if (policy == FORCED)
        {
            AwaitableFrameBase* current = this;
            while (current)
            {
                auto prev_handle = std::exchange(current->prev_, {});
                auto* prev = prev_handle
                                 ? &std::coroutine_handle<AwaitableFrameBase>::from_address(prev_handle.address()).
                                 promise()
                                 : nullptr;
                std::coroutine_handle<AwaitableFrameBase>::from_promise(*current).destroy();
                current = prev;
            }
        }
        else std::coroutine_handle<AwaitableFrameBase>::from_promise(*this).destroy();
    }

    void AwaitableFrameBase::resume()
    {
        this->coro_.resume();
    }

    void
    AwaitableFrameBase::set_calling_coroutine(std::coroutine_handle<> h)
    {
        this->prev_ = h;
    }

    std::coroutine_handle<> AwaitableFrameBase::get_coroutine_handle()
    {
        return this->coro_;
    }

    std::coroutine_handle<> AwaitableFrameBase::get_calling_coroutine()
    {
        return this->prev_;
    }

    void AwaitableFrameBase::push_frame_into_task_queue(std::coroutine_handle<> h)
    {
        system::this_thread::detail::q->enqueue(h);
#if UVENT_DEBUG
        spdlog::trace("Coroutine returned into local queue: {}", h.address());
#endif
    }

    bool AwaitableFrameBase::await_ready()
    {
        return false;
    }

    void AwaitableFrameBase::push_frame_to_be_destroyed()
    {
        system::this_thread::detail::q_c->enqueue(this->coro_);
    }

    void AwaitableFrameBase::set_next_coroutine(std::coroutine_handle<> h)
    {
        this->next_ = h;
    }

    std::coroutine_handle<> AwaitableFrameBase::get_next_coroutine()
    {
        return this->next_;
    }

    AwaitableFrame<void>::~AwaitableFrame()
    {
#if UVENT_DEBUG
        spdlog::trace("Destroying coroutine {}", this->coro_.address());
#endif
    }

    std::suspend_always AwaitableFrame<void>::initial_suspend() noexcept
    {
        return {};
    }

    std::suspend_always AwaitableFrame<void>::final_suspend() noexcept
    {
#if UVENT_DEBUG
        spdlog::trace("Entering final_suspend for void coroutine {}", this->coro_.address());
#endif
        if (this->prev_)
        {
            auto c_temp = std::coroutine_handle<::usub::uvent::detail::AwaitableFrameBase>::from_address(
                this->prev_.address());
            AwaitableFrame<void>::push_frame_into_task_queue(static_cast<std::coroutine_handle<>>(c_temp));
            std::exchange(this->prev_, nullptr);
        }
        this->push_frame_to_be_destroyed();
        return {};
    }

    std::suspend_always AwaitableFrame<void>::yield_value() noexcept
    {
        return {};
    }
}
