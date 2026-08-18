//
// Created by root on 10/21/25.
//

#ifndef TLS_UVENT_H
#define TLS_UVENT_H

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <uvent/base/Predefines.h>
#include <uvent/poll/PollerBase.h>
#include <uvent/utils/datastructures/queue/ConcurrentQueues.h>
#include <uvent/utils/datastructures/queue/FastQueue.h>

namespace usub::uvent::net
{
    struct SocketHeader;
}

namespace usub::uvent::thread
{
#ifdef UVENT_SOCKET_OWNER_FORWARDING
    /**
     * \brief Socket maintenance request forwarded to the worker that owns the
     *        socket header (see SocketHeader::owner_tid).
     *
     * With UVENT_ENABLE_REUSEADDR the poller, the timer wheel and the header
     * delete queue are thread_local. A coroutine that migrated to another
     * worker must not touch them directly: cancelTimer() would hit a foreign
     * wheel (no-op + id collision in cancelledPending_), removeEvent() a
     * foreign epoll and `delete` would free memory the owner's wheel still
     * references (heap-use-after-free in TimerWheel::tick). Instead the
     * operation is queued here and applied by the owner in its event loop
     * (Thread::processSocketOps).
     */
    struct SocketOp
    {
        enum class Kind : uint8_t
        {
            None = 0,
            /// arm (timer_id == 0) or refresh (timer_id != 0) the socket timeout
            Timeout,
            /// shutdown() from a foreign thread: cancel the socket timer and
            /// shutdown(2) the fd — both on the owner, so a stale fd number
            /// (owner already closed it) can never hit an unrelated socket
            Shutdown,
            /// full teardown: cancel timer, removeEvent, queue header for delete
            Destroy
        };

        Kind kind{Kind::None};
        net::SocketHeader* header{nullptr};
        uint64_t timeout_ms{0};
    };
#endif

    struct alignas(data_structures::metadata::CACHELINE_SIZE) ThreadLocalStorage
    {
        friend class system::Thread;

        void push_task_inbox(std::coroutine_handle<> task);

#ifdef UVENT_SOCKET_OWNER_FORWARDING
        /**
         * \brief Forward a socket maintenance op to this (owner) worker and wake it.
         *        Thread-safe; may be called from any thread except the owner itself
         *        (the owner applies ops directly).
         */
        void push_socket_op(const SocketOp& op);
#endif

        void set_poller(core::PollerImpl* p) noexcept
        {
            this->poller_.store(p, std::memory_order_release);
        }

    private:
        queue::concurrent::MPMCQueue<std::coroutine_handle<>> inbox_q_;
        std::atomic_bool is_added_new_{false};
#ifdef UVENT_SOCKET_OWNER_FORWARDING
        queue::concurrent::MPMCQueue<SocketOp> sock_ops_q_{4096};
        std::atomic_bool has_sock_ops_{false};
#endif
        std::atomic<core::PollerImpl*> poller_{nullptr};
    };
} // namespace usub::uvent::thread

#endif // TLS_UVENT_H
