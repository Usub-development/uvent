#include "uvent/poll/IocpPoller.h"

#include <cstddef>

#include "uvent/system/Settings.h"
#include "uvent/system/SystemContext.h"
#include "uvent/system/Thread.h"
#include "uvent/net/Socket.h"

namespace usub::uvent::core
{

    using usub::uvent::net::SocketHeader;

    IocpPoller::IocpPoller(utils::TimerWheel* wheel)
        : PollerBase()
        , wheel(wheel)
    {
        wsa_init_once();
        pollfds_.reserve(128);
        headers_.reserve(128);
        iocp_ = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    }

    IocpPoller::~IocpPoller()
    {
        pollfds_.clear();
        headers_.clear();
        index_by_fd_.clear();
        if (iocp_ != nullptr)
        {
            ::CloseHandle(iocp_);
            iocp_ = nullptr;
        }
    }

    void IocpPoller::set_events_for_op(WSAPOLLFD& pfd, OperationType op)
    {
        short ev = 0;
        switch (op)
        {
        case OperationType::READ:
            ev = POLLIN | POLLRDNORM | POLLERR | POLLHUP;
            break;
        case OperationType::WRITE:
            ev = POLLOUT | POLLWRNORM | POLLERR | POLLHUP;
            break;
        case OperationType::ALL:
        default:
            ev = POLLIN | POLLRDNORM | POLLOUT | POLLWRNORM | POLLERR | POLLHUP;
            break;
        }
        pfd.events = ev;
    }

    void IocpPoller::addEvent(SocketHeader* header, OperationType op)
    {
        if (!header) return;
        if (header->fd == INVALID_FD) return;

        SOCKET s = static_cast<SOCKET>(header->fd);

        if (iocp_ != nullptr)
        {
            ::CreateIoCompletionPort(reinterpret_cast<HANDLE>(s),
                                     iocp_,
                                     reinterpret_cast<ULONG_PTR>(header),
                                     0);
        }

        auto it = index_by_fd_.find(s);
        if (it != index_by_fd_.end())
        {
            auto idx = it->second;
            set_events_for_op(pollfds_[idx], op);
            return;
        }

        WSAPOLLFD pfd{};
        pfd.fd = s;
        set_events_for_op(pfd, op);

        std::size_t idx = pollfds_.size();
        pollfds_.push_back(pfd);
        headers_.push_back(header);
        index_by_fd_[s] = idx;

#ifndef UVENT_ENABLE_REUSEADDR
        if (header->is_tcp() && header->is_passive())
            system::this_thread::detail::is_started.store(true, std::memory_order_relaxed);
#else
        if (header->is_tcp() && header->is_passive())
            system::this_thread::detail::is_started = true;
#endif
    }

    void IocpPoller::updateEvent(SocketHeader* header, OperationType op)
    {
        if (!header) return;
        if (header->fd == INVALID_FD) return;

        SOCKET s = static_cast<SOCKET>(header->fd);
        auto it = index_by_fd_.find(s);
        if (it == index_by_fd_.end())
        {
            addEvent(header, op);
            return;
        }

        auto idx = it->second;
        set_events_for_op(pollfds_[idx], op);
    }

    void IocpPoller::removeEvent(SocketHeader* header, OperationType)
    {
        if (!header) return;
        if (header->fd == INVALID_FD) return;

        SOCKET s = static_cast<SOCKET>(header->fd);
        auto it = index_by_fd_.find(s);
        if (it != index_by_fd_.end())
        {
            std::size_t idx = it->second;
            std::size_t last = pollfds_.size() - 1;

            if (idx != last)
            {
                std::swap(pollfds_[idx], pollfds_[last]);
                std::swap(headers_[idx], headers_[last]);

                if (headers_[idx])
                {
                    SOCKET moved_fd = static_cast<SOCKET>(headers_[idx]->fd);
                    index_by_fd_[moved_fd] = idx;
                }
            }

            pollfds_.pop_back();
            headers_.pop_back();
            index_by_fd_.erase(it);
        }

        if (header->fd != -1)
        {
            ::closesocket(static_cast<SOCKET>(header->fd));
            header->fd = -1;
        }
    }

    bool IocpPoller::poll(int timeout_ms)
    {
#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.enter();
#endif

        int n = 0;

        if (!pollfds_.empty())
        {
            int timeout = (timeout_ms < 0) ? -1 : timeout_ms;
            n = ::WSAPoll(pollfds_.data(),
                          static_cast<ULONG>(pollfds_.size()),
                          timeout);
        }
        else
        {
            if (timeout_ms > 0)
                ::Sleep(static_cast<DWORD>(timeout_ms));
        }

        int ready_left = n;

        if (n > 0)
        {
            for (std::size_t i = 0; i < pollfds_.size() && ready_left > 0; ++i)
            {
                auto& p = pollfds_[i];
                if (p.revents == 0)
                    continue;

                --ready_left;

                auto* sock = headers_[i];
                if (!sock)
                    continue;

#ifndef UVENT_ENABLE_REUSEADDR
                if (sock->is_disconnected_now())
                    continue;
                if (!sock->try_mark_busy())
                    continue;
#endif

                const bool has_read =
                    (p.revents & (POLLIN | POLLRDNORM | POLLERR | POLLHUP)) != 0;
                const bool has_write =
                    (p.revents & (POLLOUT | POLLWRNORM | POLLERR | POLLHUP)) != 0;

                if (p.revents & (POLLERR | POLLHUP))
                {
                    sock->mark_disconnected();
                }

                if (has_read && sock->first)
                {
                    auto c = std::exchange(sock->first, nullptr);
                    system::this_thread::detail::q->enqueue(c);
                }

                if (has_write && sock->second)
                {
                    auto c = std::exchange(sock->second, nullptr);
                    system::this_thread::detail::q->enqueue(c);
                }

#ifndef UVENT_ENABLE_REUSEADDR
                sock->clear_busy();
#endif
            }

            if (static_cast<std::size_t>(n) == pollfds_.size())
            {
                pollfds_.reserve(pollfds_.size() << 1);
                headers_.reserve(headers_.size() << 1);
            }
        }

        int completed = 0;

        if (iocp_ != nullptr)
        {
            for (;;)
            {
                DWORD bytes = 0;
                ULONG_PTR key = 0;
                LPOVERLAPPED pov = nullptr;

                BOOL ok = ::GetQueuedCompletionStatus(iocp_, &bytes, &key, &pov, 0);
                if (!ok && pov == nullptr)
                {
                    break;
                }

                if (pov == nullptr)
                {
                    break;
                }

                auto* header = reinterpret_cast<SocketHeader*>(key);
                if (!header)
                    continue;

#ifndef UVENT_ENABLE_REUSEADDR
                if (!header->try_mark_busy())
                    continue;
#endif

                auto* ext = reinterpret_cast<usub::uvent::net::IocpOverlapped*>(
                    reinterpret_cast<char*>(pov) -
                    offsetof(usub::uvent::net::IocpOverlapped, ov));
                ext->bytes_transferred = bytes;

                if (!ok || bytes == 0)
                {
                    header->mark_disconnected();
                }

                switch (ext->op)
                {
                case usub::uvent::net::IocpOp::READ:
                    if (header->first)
                    {
                        auto c = std::exchange(header->first, nullptr);
                        system::this_thread::detail::q->enqueue(c);
                    }
                    break;
                case usub::uvent::net::IocpOp::WRITE:
                    if (header->second)
                    {
                        auto c = std::exchange(header->second, nullptr);
                        system::this_thread::detail::q->enqueue(c);
                    }
                    break;
                case usub::uvent::net::IocpOp::ACCEPT:
                    if (header->first)
                    {
                        auto c = std::exchange(header->first, nullptr);
                        system::this_thread::detail::q->enqueue(c);
                    }
                    break;
                case usub::uvent::net::IocpOp::CONNECT:
                    if (header->second)
                    {
                        auto c = std::exchange(header->second, nullptr);
                        system::this_thread::detail::q->enqueue(c);
                    }
                    break;
                }

#ifndef UVENT_ENABLE_REUSEADDR
                header->clear_busy();
#endif
                ++completed;
            }
        }

#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.leave();
#endif
        return (n > 0) || (completed > 0);
    }

    bool IocpPoller::try_lock()
    {
        if (this->lock.try_acquire())
        {
            this->is_locked.store(true, std::memory_order_release);
            return true;
        }
        return false;
    }

    void IocpPoller::unlock()
    {
        this->is_locked.store(false, std::memory_order_release);
        this->lock.release();
    }

    void IocpPoller::lock_poll(int timeout_ms)
    {
        this->lock.acquire();
        this->is_locked.store(true, std::memory_order_release);
        this->poll(timeout_ms);
        this->unlock();
    }

} // namespace usub::uvent::core