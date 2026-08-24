#ifndef UVENT_TASKS_TASKSTATE_H
#define UVENT_TASKS_TASKSTATE_H

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <new>

#include "uvent/sync/CancelState.h"
#include "uvent/utils/datastructures/queue/IntrusiveMPSC.h"

namespace usub::uvent::detail
{
    class AwaitableFrameBase;
}

namespace usub::uvent::task
{
    struct TaskStateBase : sync::CancelState, queue::concurrent::MPSCNode
    {
        std::coroutine_handle<> root{};
        std::atomic<int> owner_tid{-1};
        std::atomic<uint8_t> status{0};
        std::atomic<bool> kick_pending{false};
        sync::WaitList done_waiters;
        void (*store_result)(TaskStateBase*, detail::AwaitableFrameBase*){nullptr};

        TaskStateBase() { this->is_task = true; }

        [[nodiscard]] bool done() const noexcept { return this->status.load(std::memory_order_acquire) != 0; }

        void complete(detail::AwaitableFrameBase* frame) noexcept;

        void kick() noexcept;

        void process_kick() noexcept;
    };

    template <class T>
    struct TaskState final : TaskStateBase
    {
        alignas(T) unsigned char result[sizeof(T)]{};
        bool has_result{false};
        std::exception_ptr exc{nullptr};

        void set_value(T&& v)
        {
            new (&this->result) T(std::move(v));
            this->has_result = true;
        }

        T take_value()
        {
            if (this->exc)
                std::rethrow_exception(this->exc);
            return std::move(*std::launder(reinterpret_cast<T*>(&this->result)));
        }

        ~TaskState() override
        {
            if (this->has_result)
                std::launder(reinterpret_cast<T*>(&this->result))->~T();
        }
    };

    template <>
    struct TaskState<void> final : TaskStateBase
    {
        std::exception_ptr exc{nullptr};

        void take_value() const
        {
            if (this->exc)
                std::rethrow_exception(this->exc);
        }
    };
} // namespace usub::uvent::task

#endif // UVENT_TASKS_TASKSTATE_H
