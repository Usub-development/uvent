//
// Created by kirill on 11/15/24.
//

#include "include/uvent/poll/EPoller.h"
#include "include/uvent/system/Settings.h"
#include "include/uvent/utils/thread/ThreadStats.h"
#include "include/uvent/system/SystemContext.h"

namespace usub::uvent::core
{
    EPoller::EPoller(utils::TimerWheel* wheel) : PollerBase(settings::timeout_duration_ms),
                                                 wheel(wheel)
    {
        this->poll_fd = epoll_create1(0);
        sigemptyset(&this->sigmask);
        this->events.resize(1000);
    }

    void EPoller::addEvent(net::SocketHeader* socket, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(socket);
        switch (initialState)
        {
        case READ:
            event.events = EPOLLIN | EPOLLET;
            break;
        case WRITE:
            event.events = EPOLLOUT | EPOLLET;
            break;
        default:
            event.events = (EPOLLIN | EPOLLOUT | EPOLLET);
            break;
        }
#if UVENT_DEBUG
        spdlog::info("Socket added: {}", socket->fd);
#endif
        epoll_ctl(this->poll_fd, EPOLL_CTL_ADD, socket->fd, &event);
        if (!(socket->socket_info & static_cast<uint8_t>(net::Proto::TCP)))
        {
            uint64_t timerId = 0;
            auto timer = new utils::Timer(this->timeoutDuration_ms, socket->fd, utils::TIMEOUT);
            socket->timer_id = this->wheel->addTimer(timer);
            auto coro = utils::timeout_coroutine([this, fd = socket->fd, timerId]
            {
                removeEvent(fd, timerId, ALL);
            });
            timer->coro = coro.get_promise()->get_coroutine_handle();
        }
        else if ((socket->socket_info & static_cast<uint8_t>(net::Proto::TCP) && (socket->socket_info & static_cast<
            uint8_t>(net::Role::PASSIVE))))
        {
            utils::detail::thread::is_started.store(true, std::memory_order_relaxed);
        }
    }

    void EPoller::updateEvent(net::SocketHeader* socket, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(socket);
        event.events = 0;
        if (socket->is_writing_now()) event.events |= (EPOLLOUT | EPOLLET);
        if (socket->is_reading_now()) event.events |= (EPOLLIN | EPOLLET);

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
        spdlog::info("Updating socket #{} with events: {} (READ: {}, WRITE: {})",
                     socket->fd, static_cast<int>(event.events),
                     static_cast<bool>(event.events & EPOLLIN),
                     static_cast<bool>(event.events & EPOLLOUT));
        spdlog::info("Socket #{} updated with state: {}, read state: {}, write state: {}", socket->fd,
                     static_cast<int>(initialState),
                     socket->is_reading_now(),
                     socket->is_writing_now());
#endif

        int result = epoll_ctl(this->poll_fd, EPOLL_CTL_MOD, socket->fd, &event);
#if UVENT_DEBUG
        if (result < 0)
        {
            if (errno == ENOENT || errno == EBADF || errno == ENOTSOCK)
            {
                spdlog::info("Socket #{} is closed or invalid, ignoring epoll_ctl modification.", socket->fd);
                return;
            }
            throw std::system_error(errno, std::generic_category(),
                                    "epoll_ctl[EPOLL_CTL_MOD] (EpollPoller::updateEvent)");
        }
#endif
    }

    void EPoller::removeEvent(int fd, uint64_t timerId, OperationType op)
    {
#if UVENT_DEBUG
        spdlog::info("Socket removed: {}", fd);
#endif

        epoll_ctl(this->poll_fd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        this->wheel->removeTimer(timerId);
    }

    bool EPoller::poll(int timeout)
    {
        int n = epoll_pwait(this->poll_fd, this->events.data(), static_cast<int>(this->events.size()),
                            timeout, &this->sigmask);
        system::this_thread::detail::g_qsbr.enter();
#if UVENT_DEBUG
        if (n < 0 && errno != EINTR) throw std::system_error(errno, std::generic_category(), "epoll_pwait");
#endif
        for (int i = 0; i < n; i++)
        {
            auto& event = this->events[i];
            auto* sock = static_cast<net::SocketHeader*>(event.data.ptr);
            if (sock->is_busy_now()) continue;
            if (!(sock->socket_info & static_cast<uint8_t>(net::Proto::TCP) & (sock->socket_info & static_cast<uint8_t>(
                    net::Role::PASSIVE))) &
                (event.events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)))
            {
                this->removeEvent(sock->fd, sock->timer_id, ALL);
                continue;
            }
            sock->try_mark_busy();
            if (event.events & EPOLLIN)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as IN", sock->fd);
#endif
                system::this_thread::detail::q->enqueue(sock->first);
                sock->first = nullptr;
                if (!(event.events & EPOLLOUT)) continue;
            }
            if (event.events & EPOLLOUT)
            {
#if UVENT_DEBUG
                spdlog::info("Socket #{} triggered as OUT", sock->fd);
#endif
                if (sock->second)
                {
                    if (!(sock->socket_info & static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING)))
                        system::this_thread::detail::q->enqueue(sock->second);
                    else
                    {
                        int err = 0;
                        socklen_t len = sizeof(err);
                        getsockopt(event.data.fd, SOL_SOCKET, SO_ERROR, &err, &len);
                        if (err != 0)
                        {
                            sock->socket_info &= ~static_cast<uint8_t>(net::AdditionalState::CONNECTION_PENDING);
                            sock->socket_info &= static_cast<uint8_t>(net::AdditionalState::CONNECTION_FAILED);
                        }
                    }
                    sock->second = nullptr;
                }
            }
        }
        if (n == this->events.size()) this->events.resize(this->events.size() << 1);
        this->unlock();
        system::this_thread::detail::g_qsbr.leave();
        return n > 0;
    }

    bool EPoller::try_lock()
    {
        this->is_locked.store(true, std::memory_order_seq_cst);
        return this->lock.try_acquire();
    }

    void EPoller::unlock()
    {
        this->lock.release();
        this->is_locked.store(false, std::memory_order_seq_cst);
    }

    void EPoller::lock_poll(int timeout)
    {
        this->lock.acquire();
        this->poll(timeout);
    }
}
