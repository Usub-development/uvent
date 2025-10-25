//
// Created by kirill on 11/15/24.
//

#include "uvent/poll/EPoller.h"
#include "uvent/system/Settings.h"
#include "uvent/system/SystemContext.h"
#include "uvent/net/Socket.h"

namespace usub::uvent::core
{
    EPoller::EPoller(utils::TimerWheel* wheel) : PollerBase(),
                                                 wheel(wheel)
    {
        this->poll_fd = epoll_create1(0);
        sigemptyset(&this->sigmask);
        this->events.resize(1000);
    }

    void EPoller::addEvent(net::SocketHeader* header, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(header);

        if (header->is_tcp() && !header->is_passive())
            event.events = EPOLLET;

        switch (initialState)
        {
        case READ: event.events |= EPOLLIN;
            break;
        case WRITE: event.events |= EPOLLOUT;
            break;
        default: event.events |= EPOLLIN | EPOLLOUT;
            break;
        }

#if UVENT_DEBUG
        spdlog::info("Socket added: fd={} et={} in={} out={}",
                     header->fd,
                     bool(event.events & EPOLLET),
                     bool(event.events & EPOLLIN),
                     bool(event.events & EPOLLOUT));
#endif

        epoll_ctl(this->poll_fd, EPOLL_CTL_ADD, header->fd, &event);

#ifndef UVENT_ENABLE_REUSEADDR
        if (header->is_tcp() && header->is_passive()) system::this_thread::detail::is_started.store(
            true, std::memory_order_relaxed);
#else
        if (header->is_tcp() && header->is_passive()) system::this_thread::detail::is_started = true;
#endif
    }

    void EPoller::updateEvent(net::SocketHeader* header, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(header);
        if (!(header->is_tcp() && header->is_passive())) event.events = EPOLLET;
        if (header->is_writing_now()) event.events |= EPOLLOUT;
        if (header->is_reading_now()) event.events |= EPOLLIN;

        switch (initialState)
        {
        case READ:
            event.events |= EPOLLIN | EPOLLET;
            break;
        case WRITE:
            event.events |= EPOLLOUT | EPOLLET;
            break;
        default:
            event.events |= (EPOLLIN | EPOLLOUT | EPOLLET);
            break;
        }

#if UVENT_DEBUG
        spdlog::info("Updating socket #{} READ: {}, WRITE: {}",
                     header->fd,
                     static_cast<bool>(event.events & EPOLLIN),
                     static_cast<bool>(event.events & EPOLLOUT));
        spdlog::info("Socket #{} updated with state: {}, read state: {}, write state: {}", header->fd,
                     static_cast<int>(initialState),
                     header->is_reading_now(),
                     header->is_writing_now());
#endif

        int result = epoll_ctl(this->poll_fd, EPOLL_CTL_MOD, header->fd, &event);
#if UVENT_DEBUG
        if (result < 0)
        {
            if (errno == ENOENT || errno == EBADF || errno == ENOTSOCK)
            {
                spdlog::info("Socket #{} is closed or invalid, ignoring epoll_ctl modification.", header->fd);
                return;
            }
            throw std::system_error(errno, std::generic_category(),
                                    "epoll_ctl[EPOLL_CTL_MOD] (EpollPoller::updateEvent)");
        }
#endif
    }

    void EPoller::removeEvent(net::SocketHeader* header, OperationType)
    {
#if UVENT_DEBUG
        spdlog::info("Socket removed: {}", header->fd);
#endif
        using namespace usub::utils::sync::refc;

        epoll_ctl(this->poll_fd, EPOLL_CTL_DEL, header->fd, nullptr);
        ::close(header->fd);
    }

    bool EPoller::poll(int timeout)
    {
        int n = epoll_pwait(this->poll_fd, this->events.data(), static_cast<int>(this->events.size()),
                            timeout, &this->sigmask);
#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.enter();
#endif
#if UVENT_DEBUG
        if (n < 0 && errno != EINTR) throw std::system_error(errno, std::generic_category(), "epoll_pwait");
#endif
        for (int i = 0; i < n; i++)
        {
            auto& event = this->events[i];
            auto* sock = static_cast<net::SocketHeader*>(event.data.ptr);
#ifndef UVENT_ENABLE_REUSEADDR
            if (sock->is_busy_now() || sock->is_disconnected_now()) continue;
#endif
            if (const bool is_listener = sock->is_tcp() && sock->is_passive(); !is_listener && (event.events & (EPOLLHUP
                | EPOLLRDHUP | EPOLLERR)))
            {
                this->removeEvent(sock, ALL);
                continue;
            }
#ifndef UVENT_ENABLE_REUSEADDR
            sock->try_mark_busy();
#endif
            if (event.events & EPOLLIN && sock->first)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as IN", sock->fd);
#endif
                auto c = std::exchange(sock->first, nullptr);
                system::this_thread::detail::q->enqueue(c);
                if (!(event.events & EPOLLOUT)) continue;
            }
            if (event.events & EPOLLOUT && sock->second)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as OUT", sock->fd);
#endif
                if (sock->second)
                {
                    if (!(sock->socket_info & static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING)))
                    {
                        auto c = std::exchange(sock->second, nullptr);
                        system::this_thread::detail::q->enqueue(c);
                    }
                    else
                    {
                        int err = 0;
                        socklen_t len = sizeof(err);
                        getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                        sock->socket_info &= ~static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING);
                        if (err != 0)
                        {
                            sock->socket_info |= static_cast<uint8_t>(net::AdditionalState::CONNECTION_FAILED);
                        }
                    }
                }
            }
        }
        if (n == this->events.size()) this->events.resize(this->events.size() << 1);
#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.leave();
#endif
        return n > 0;
    }

    bool EPoller::try_lock()
    {
        if (this->lock.try_acquire())
        {
            this->is_locked.store(true, std::memory_order_release);
            return true;
        }
        return false;
    }

    void EPoller::unlock()
    {
        this->is_locked.store(false, std::memory_order_release);
        this->lock.release();
    }

    void EPoller::lock_poll(int timeout)
    {
        this->lock.acquire();
        this->is_locked.store(true, std::memory_order_release);
        this->poll(timeout);
        this->unlock();
    }
}
