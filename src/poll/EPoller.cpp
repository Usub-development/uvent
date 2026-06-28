//
// Created by kirill on 11/15/24.
//

#include "uvent/poll/EPoller.h"

#include "uvent/net/Socket.h"
#include "uvent/system/Settings.h"
#include "uvent/system/SystemContext.h"

namespace usub::uvent::core
{
    EPoller::EPoller(utils::TimerWheel& wheel) : wheel(wheel)
    {
        // CLOEXEC, чтобы не утекало в форки/exec
        this->poll_fd = epoll_create1(EPOLL_CLOEXEC);
        // Прежняя строка ставила SO_PREFER_BUSY_POLL на epoll_fd — это noop, т. к.
        // SO_PREFER_BUSY_POLL — socket option, и должен ставиться на data-сокеты,
        // плюс требует SO_BUSY_POLL > 0. Переехало в async_accept (см. SocketLinux.h).
        // Зафиксированный размер: 4096 events за один epoll_wait — достаточно;
        // если не хватит, следующий epoll_wait считает остаток. Раньше events.resize(<<1)
        // на каждом переполнении делал realloc/копию.
        this->events.resize(4096);
    }

    void EPoller::addEvent(net::SocketHeader* header, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(header);
        event.events = 0;
        event.events = (EPOLLIN | EPOLLOUT | EPOLLET);

#if UVENT_DEBUG
        spdlog::info("Socket added: fd={} et={} in={} out={}", header->fd, bool(event.events & EPOLLET),
                     bool(event.events & EPOLLIN), bool(event.events & EPOLLOUT));
#endif

        epoll_ctl(this->poll_fd, EPOLL_CTL_ADD, header->fd, &event);
    }


    void EPoller::updateEvent(net::SocketHeader* header, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(header);
        event.events = 0;

        if (header->is_tcp() && !header->is_passive())
            event.events = (EPOLLIN | EPOLLOUT | EPOLLET);
        else
        {
            event.events = EPOLLIN | EPOLLET;
#ifndef UVENT_ENABLE_REUSEADDR
            if (header->is_tcp() && header->is_passive())
                system::this_thread::detail::is_started.store(true, std::memory_order_relaxed);
#else
            if (header->is_tcp() && header->is_passive())
                system::this_thread::detail::is_started = true;
#endif
        }

#if UVENT_DEBUG
        spdlog::info("Updating socket #{} READ: {}, WRITE: {}", header->fd, static_cast<bool>(event.events & EPOLLIN),
                     static_cast<bool>(event.events & EPOLLOUT));
        spdlog::info("Socket #{} updated with state: {}, read state: {}, write state: {}", header->fd,
                     static_cast<int>(initialState), header->is_reading_now(), header->is_writing_now());
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

    void EPoller::removeEvent(net::SocketHeader* header)
    {
#if UVENT_DEBUG
        spdlog::info("Socket removed: {}", header->fd);
#endif
        using namespace usub::utils::sync::refc;

        epoll_ctl(this->poll_fd, EPOLL_CTL_DEL, header->fd, nullptr);
        ::close(header->fd);
        header->fd = -1;
    }

    bool EPoller::poll(int timeout)
    {
        // epoll_wait вместо epoll_pwait: sigmask пустой, переключение sigprocmask
        // в ядре стоит ~150 нс на каждый syscall, а смысла не несёт.
        int n = ::epoll_wait(this->poll_fd, this->events.data(), static_cast<int>(this->events.size()), timeout);
#ifndef UVENT_ENABLE_REUSEADDR
        system::this_thread::detail::g_qsbr.enter();
#endif
#if UVENT_DEBUG
        if (n < 0 && errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "epoll_wait");
#endif
        for (int i = 0; i < n; i++)
        {
            auto& event = this->events[i];
            auto* sock = static_cast<net::SocketHeader*>(event.data.ptr);
#ifndef UVENT_ENABLE_REUSEADDR
            if (sock->is_busy_now() || sock->is_disconnected_now())
                continue;
#endif
            bool hup = !(sock->is_tcp() && sock->is_passive()) && (event.events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR));
            if (hup)
                sock->mark_disconnected();
#ifndef UVENT_ENABLE_REUSEADDR
            sock->try_mark_busy();
#endif
            if (event.events & EPOLLIN)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as IN", sock->fd);
#endif
                if (sock->first)
                {
                    auto c = std::exchange(sock->first, nullptr);
                    system::this_thread::detail::q.enqueue(c);
                }
                else
                {
                    sock->mark_read_pending();
                }
            }
            if (event.events & EPOLLOUT)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as OUT", sock->fd);
#endif
                if (!(sock->socket_info & static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING)))
                {
                    if (sock->second)
                    {
                        auto c = std::exchange(sock->second, nullptr);
                        system::this_thread::detail::q.enqueue(c);
                    }
                    else
                    {
                        sock->mark_write_pending();
                    }
                }
                else
                {
                    int err = 0;
                    socklen_t len = sizeof(err);
                    getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                    sock->socket_info &= ~static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING);
                    if (err != 0)
                        sock->socket_info |= static_cast<uint8_t>(net::AdditionalState::CONNECTION_FAILED);
                    else if (sock->second)
                    {
                        auto c = std::exchange(sock->second, nullptr);
                        system::this_thread::detail::q.enqueue(c);
                    }
                    else
                    {
                        sock->mark_write_pending();
                    }
                }
            }
            if (hup)
            {
                this->removeEvent(sock);
#if UVENT_DEBUG
                spdlog::debug("Socket hup/err fd={}", sock->fd);
#endif
            }
        }
        // Раньше при n == events.size() вектор удваивался через resize(size << 1),
        // что стоило realloc + memcpy. Не удваиваем: если уперлись в 4096 — ОК,
        // следующий epoll_wait дозаберёт остаток. Тонкий хвост latency скрадывается
        // тем, что мы и так бьём итерации в цикле.
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
    void EPoller::deregisterEvent(net::SocketHeader* header) const
    {
        epoll_ctl(this->poll_fd, EPOLL_CTL_DEL, header->fd, nullptr);
    }

    int EPoller::get_poll_fd() { return this->poll_fd; }
} // namespace usub::uvent::core
