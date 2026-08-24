//
// Created by root on 10/21/25.
//

#include <uvent/pool/TLS.h>

#ifdef OS_LINUX
#ifndef UVENT_ENABLE_IO_URING
#include <uvent/poll/EPoller.h>
#else
#include <uvent/poll/IOUringPoller.h>
#endif
#elif defined(OS_BSD) || defined(OS_APPLE)
#include <uvent/poll/KPoller.h>
#else
#include <uvent/poll/IocpPoller.h>
#endif

namespace usub::uvent::thread
{
    void ThreadLocalStorage::push_task_inbox(std::coroutine_handle<> task)
    {
        if (!task)
            return;
        auto* frame = &std::coroutine_handle<detail::AwaitableFrameBase>::from_address(task.address()).promise();
        this->inbox_q_.push(frame);

        this->is_added_new_.store(true, std::memory_order_release);

        if (auto* p = this->poller_.load(std::memory_order_acquire))
            p->wake();
    }

    void ThreadLocalStorage::wake_poller() noexcept
    {
        if (auto* p = this->poller_.load(std::memory_order_acquire))
            p->wake();
    }

    void ThreadLocalStorage::push_cancel_kick(uvent::task::TaskStateBase* t)
    {
        this->kick_q_.push(t);

        this->has_kicks_.store(true, std::memory_order_release);

        if (auto* p = this->poller_.load(std::memory_order_acquire))
            p->wake();
    }

#ifdef UVENT_SOCKET_OWNER_FORWARDING
    void ThreadLocalStorage::push_socket_op(const SocketOp& op)
    {
        this->sock_ops_q_.enqueue(op);

        this->has_sock_ops_.store(true, std::memory_order_release);

        if (auto* p = this->poller_.load(std::memory_order_acquire))
            p->wake();
    }
#endif
} // namespace usub::uvent::thread
