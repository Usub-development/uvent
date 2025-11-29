#include "uvent/poll/IOUringPoller.h"

#include <system_error>
#include <unistd.h>
#include <cstring>

#include "uvent/system/SystemContext.h"
#include "uvent/system/Settings.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::core
{
    using namespace usub::uvent::core::detail;

    IOUringPoller::IOUringPoller(utils::TimerWheel& wheel_)
        : wheel(wheel_)
    {
        std::memset(&this->ring, 0, sizeof(this->ring));

        struct io_uring_params params;
        std::memset(&params, 0, sizeof(params));

        int ret = ::io_uring_queue_init_params(this->ring_entries, &this->ring, &params);
        if (ret < 0)
        {
#if UVENT_DEBUG
            throw std::system_error(-ret, std::generic_category(),
                                    "io_uring_queue_init_params failed");
#else
            std::abort();
#endif
        }

        sigemptyset(&this->sigmask);

#if UVENT_DEBUG
        spdlog::info("IOUringPoller ctor: entries={}", this->ring_entries);
#endif
    }

    IOUringPoller::~IOUringPoller()
    {
        ::io_uring_queue_exit(&this->ring);
    }

    void IOUringPoller::addEvent(net::SocketHeader*, OperationType)
    {
    }

    void IOUringPoller::updateEvent(net::SocketHeader*, OperationType)
    {
    }

    void IOUringPoller::removeEvent(net::SocketHeader*)
    {
    }

    void IOUringPoller::submit_recv(RecvOp* op, int fd)
    {
        if (!op || fd < 0) return;

        auto* sqe = ::io_uring_get_sqe(&this->ring);
        if (!sqe) return;

        ::io_uring_prep_recv(sqe, fd, op->buf, op->len, 0);
        ::io_uring_sqe_set_data(sqe, op);
    }

    void IOUringPoller::submit_send(SendOp* op, int fd)
    {
        if (!op || fd < 0) return;

        auto* sqe = ::io_uring_get_sqe(&this->ring);
        if (!sqe) return;

        ::io_uring_prep_send(sqe, fd, op->buf, op->len, 0);
        ::io_uring_sqe_set_data(sqe, op);
    }

    void IOUringPoller::submit_accept(AcceptOp* op, int fd)
    {
        if (!op || fd < 0) return;

        auto* sqe = ::io_uring_get_sqe(&this->ring);
        if (!sqe) return;

        op->addrlen = sizeof(sockaddr_storage);
        ::io_uring_prep_accept(sqe,
                               fd,
                               reinterpret_cast<sockaddr*>(&op->addr),
                               &op->addrlen,
                               SOCK_NONBLOCK | SOCK_CLOEXEC);
        ::io_uring_sqe_set_data(sqe, op);
    }

    void IOUringPoller::submit_sendfile(SendFileOp* op, int out_fd)
    {
        if (!op || out_fd < 0 || op->in_fd < 0) return;

        auto* sqe = ::io_uring_get_sqe(&this->ring);
        if (!sqe) return;

        io_uring_prep_splice(
            sqe,
            op->in_fd,
            op->offset ? *op->offset : -1,
            out_fd,
            -1,
            op->count,
            0
        );
        ::io_uring_sqe_set_data(sqe, op);
    }

    void IOUringPoller::handle_cqe(struct io_uring_cqe* cqe)
    {
        auto* base = static_cast<IoOpBase*>(::io_uring_cqe_get_data(cqe));
        if (!base) return;

        base->res = cqe->res;
        base->err = (cqe->res < 0) ? -cqe->res : 0;
        base->completed = true;

        if (base->coro && !base->coro.done())
        {
            usub::uvent::system::this_thread::detail::q->enqueue(base->coro);
        }
    }

    bool IOUringPoller::poll(int timeout_ms)
    {
        __kernel_timespec ts{};
        __kernel_timespec* tsp = nullptr;

        if (timeout_ms >= 0)
        {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms % 1000) * 1'000'000L;
            tsp = &ts;
        }

        ::io_uring_submit(&this->ring);

#ifndef UVENT_ENABLE_REUSEADDR
        usub::uvent::system::this_thread::detail::g_qsbr.enter();
#endif

        bool any = false;
        struct io_uring_cqe* cqe = nullptr;

        int ret = ::io_uring_wait_cqe_timeout(&this->ring, &cqe, tsp);
        if (ret == -ETIME || ret == -EINTR)
        {
        }
        else if (ret < 0)
        {
#if UVENT_DEBUG
            throw std::system_error(-ret, std::generic_category(),
                                    "io_uring_wait_cqe_timeout failed");
#endif
        }
        else
        {
            this->handle_cqe(cqe);
            ::io_uring_cqe_seen(&this->ring, cqe);
            any = true;

            static constexpr unsigned MAX_BATCH = 128;
            struct io_uring_cqe* cqes[MAX_BATCH];
            unsigned count = ::io_uring_peek_batch_cqe(&this->ring, cqes, MAX_BATCH);
            for (unsigned i = 0; i < count; ++i)
            {
                this->handle_cqe(cqes[i]);
                ::io_uring_cqe_seen(&this->ring, cqes[i]);
            }
        }

#ifndef UVENT_ENABLE_REUSEADDR
        usub::uvent::system::this_thread::detail::g_qsbr.leave();
#endif

        return any;
    }

    bool IOUringPoller::try_lock()
    {
        if (this->lock.try_acquire())
        {
            this->is_locked.store(true, std::memory_order_release);
            return true;
        }
        return false;
    }

    void IOUringPoller::unlock()
    {
        this->is_locked.store(false, std::memory_order_release);
        this->lock.release();
    }

    void IOUringPoller::lock_poll(int timeout_ms)
    {
        this->lock.acquire();
        this->is_locked.store(true, std::memory_order_release);
        this->poll(timeout_ms);
        this->unlock();
    }
} // namespace usub::uvent::core