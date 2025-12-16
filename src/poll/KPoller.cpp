//
// Created by kirill on 11/16/24.
//

#include "uvent/poll/KPoller.h"
#include "uvent/system/Settings.h"
#include "uvent/system/SystemContext.h"
#include "uvent/net/Socket.h"
#include <cerrno>

namespace usub::uvent::core
{
    KQueuePoller::KQueuePoller(utils::TimerWheel& wheel) :
        wheel(wheel)
    {
        this->poll_fd = ::kqueue();
        if (this->poll_fd == -1)
            throw std::system_error(errno, std::generic_category(), "kqueue()");
        sigemptyset(&this->sigmask);
        this->events.resize(1024);
    }

    void KQueuePoller::addEvent(net::SocketHeader* header, OperationType initialState)
    {
        const bool edge_like = !(header->is_tcp() && header->is_passive());

        switch (initialState)
        {
        case READ:
            enable_read(header, true, edge_like);
            enable_write(header, false, edge_like);
            break;
        case WRITE:
            enable_read(header, false, edge_like);
            enable_write(header, true, edge_like);
            break;
        case ALL:
            enable_read(header, true, edge_like);
            enable_write(header, true, edge_like);
            break;
        }
    }

    void KQueuePoller::updateEvent(net::SocketHeader* header, OperationType initialState)
    {
        const bool edge_like = !(header->is_tcp() && header->is_passive());

        switch (initialState)
        {
        case READ:
            enable_read(header, true, edge_like);
            enable_write(header, false, edge_like);
            break;
        case WRITE:
            enable_read(header, false, edge_like);
            enable_write(header, true, edge_like);
            break;
        default:
            enable_read(header, true, edge_like);
            enable_write(header, true, edge_like);
            break;
        }
    }

    void KQueuePoller::removeEvent(net::SocketHeader* header, OperationType)
    {
        struct kevent ev{};
        EV_SET(&ev, header->fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
        kevent(this->poll_fd, &ev, 1, nullptr, 0, nullptr);
        EV_SET(&ev, header->fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
        kevent(this->poll_fd, &ev, 1, nullptr, 0, nullptr);

        ::close(header->fd);
        header->fd = -1;
    }

    bool KQueuePoller::poll(int timeout_ms)
    {
        struct timespec ts{};
        if (timeout_ms < 0)
        {
            ts = timespec{0, 0};
        }
        else
        {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;
        }

#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.enter();
#endif

        int n = kevent(this->poll_fd,
                       nullptr, 0,
                       this->events.data(), static_cast<int>(this->events.size()),
                       (timeout_ms < 0 ? nullptr : &ts));

#if UVENT_DEBUG
        if (n < 0 && errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "kevent(poll)");
#endif

        for (int i = 0; i < n; ++i)
        {
            auto& ev = this->events[i];
            auto* sock = static_cast<net::SocketHeader*>(ev.udata);
            if (!sock)
                continue;

#ifndef UVENT_ENABLE_REUSEADDR
            if (sock->is_busy_now() || sock->is_disconnected_now()) continue;
#endif

            bool is_err = (ev.flags & EV_ERROR) && ev.data != 0;
            bool is_eof = (ev.flags & EV_EOF);
            bool hup = !(sock->is_tcp() && sock->is_passive()) && (is_err || is_eof);

            if (hup)
            {
                // помечаем как разорванный, но НЕ закрываем fd.
                sock->mark_disconnected();
#if UVENT_DEBUG
                spdlog::debug("Socket hup/err fd={}, eof={}, err={}, data={}",
                              sock->fd, is_eof, is_err, (long long)ev.data);
#endif
            }

#ifndef UVENT_ENABLE_REUSEADDR
            sock->try_mark_busy();
#endif

            if (ev.filter == EVFILT_READ && sock->first)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as IN", sock->fd);
#endif
                auto c = std::exchange(sock->first, nullptr);
                system::this_thread::detail::q->enqueue(c);
            }

            if (ev.filter == EVFILT_WRITE && sock->second)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as OUT", sock->fd);
#endif
                if (!(sock->socket_info &
                    static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING)))
                {
                    auto c = std::exchange(sock->second, nullptr);
                    system::this_thread::detail::q->enqueue(c);
                }
                else
                {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    sock->socket_info &=
                        ~static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING);
                    if (err != 0)
                    {
                        sock->socket_info |=
                            static_cast<uint8_t>(net::AdditionalState::CONNECTION_FAILED);
#if UVENT_DEBUG
                        spdlog::debug("Connect failed on fd={} err={}", sock->fd, err);
#endif
                    }
                    else
                    {
                        auto c = std::exchange(sock->second, nullptr);
                        system::this_thread::detail::q->enqueue(c);
                    }
                }
            }
        }

        if (n == static_cast<int>(this->events.size()))
            this->events.resize(this->events.size() << 1);

#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.leave();
#endif
        return n > 0;
    }


    bool KQueuePoller::try_lock()
    {
        if (this->lock.try_acquire())
        {
            this->is_locked.store(true, std::memory_order_release);
            return true;
        }
        return false;
    }

    void KQueuePoller::unlock()
    {
        this->is_locked.store(false, std::memory_order_release);
        this->lock.release();
    }

    void KQueuePoller::lock_poll(int timeout_ms)
    {
        this->lock.acquire();
        this->is_locked.store(true, std::memory_order_release);
        this->poll(timeout_ms);
        this->unlock();
    }

    int KQueuePoller::get_poll_fd() const
    {
        return this->poll_fd;
    }
} // namespace usub::uvent::core
