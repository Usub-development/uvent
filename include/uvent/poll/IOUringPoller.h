#ifndef UVENT_IOURINGPOLLER_H
#define UVENT_IOURINGPOLLER_H

#include <atomic>
#include <csignal>
#include <semaphore>
#include <vector>
#include <coroutine>

#include <liburing.h>

#include "uvent/poll/PollerBase.h"
#include "uvent/system/Defines.h"
#include "uvent/utils/timer/TimerWheel.h"
#include "uvent/net/SocketMetadata.h"

namespace usub::uvent::core
{
    namespace detail
    {
        enum class IoOpKind : uint8_t
        {
            Recv,
            Send,
            Accept,
            SendFile,
            Connect
        };

        struct IoOpBase
        {
            IoOpKind kind{};
            net::SocketHeader* header{nullptr};
            std::coroutine_handle<> coro{};
            ssize_t res{0};
            int err{0};
            bool completed{false};
            bool timed_out{false};

            virtual ~IoOpBase() = default;
        };

        struct RecvOp : IoOpBase
        {
            uint8_t* buf{nullptr};
            size_t len{0};
        };

        struct SendOp : IoOpBase
        {
            const uint8_t* buf{nullptr};
            size_t len{0};
        };

        struct AcceptOp : IoOpBase
        {
            sockaddr_storage addr{};
            socklen_t addrlen{sizeof(sockaddr_storage)};
        };

        struct SendFileOp : IoOpBase
        {
            int in_fd{-1};
            off_t* offset{nullptr};
            size_t count{0};
        };

        struct ConnectOp : IoOpBase
        {
            sockaddr_storage addr{};
            socklen_t addrlen{0};
        };
    } // namespace detail

    class IOUringPoller
    {
    public:
        explicit IOUringPoller(utils::TimerWheel& wheel);
        ~IOUringPoller();

        void addEvent(net::SocketHeader* header, OperationType op);
        void updateEvent(net::SocketHeader* header, OperationType op);
        void removeEvent(net::SocketHeader* header);

        bool poll(int timeout_ms);

        bool try_lock();
        void unlock();
        void lock_poll(int timeout_ms);

        void submit_recv(detail::RecvOp* op, int fd);
        void submit_send(detail::SendOp* op, int fd);
        void submit_accept(detail::AcceptOp* op, int fd);
        void submit_sendfile(detail::SendFileOp* op, int out_fd);
        void submit_connect(detail::ConnectOp* op, int fd);

    private:
        void handle_cqe(struct io_uring_cqe* cqe);

    private:
        utils::TimerWheel& wheel;

        std::binary_semaphore lock{1};
        std::atomic_bool is_locked{false};

        struct io_uring ring{};
        unsigned int ring_entries{1024};

        sigset_t sigmask{};
    };
} // namespace usub::uvent::core

#endif // UVENT_IOURINGPOLLER_H
