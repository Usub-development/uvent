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
        while (!this->inbox_q_.try_enqueue(task))
            cpu_relax();

        this->is_added_new_.store(true, std::memory_order_release);

        if (auto* p = this->poller_.load(std::memory_order_acquire))
            p->wake();
    }

#ifdef UVENT_SOCKET_OWNER_FORWARDING
    void ThreadLocalStorage::push_socket_op(const SocketOp& op)
    {
        while (!this->sock_ops_q_.try_enqueue(op))
            cpu_relax();

        this->has_sock_ops_.store(true, std::memory_order_release);

        if (auto* p = this->poller_.load(std::memory_order_acquire))
            p->wake();
    }
#endif
} // namespace usub::uvent::thread
