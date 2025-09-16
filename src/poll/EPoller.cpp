//
// Created by kirill on 11/15/24.
//

#include "uvent/poll/EPoller.h"
#include "uvent/system/Settings.h"
#include "uvent/utils/thread/ThreadStats.h"
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
        switch (initialState)
        {
        case READ: event.events = EPOLLIN | EPOLLET;
            break;
        case WRITE: event.events = EPOLLOUT | EPOLLET;
            break;
        default: event.events = EPOLLIN | EPOLLOUT | EPOLLET;
            break;
        }
#if UVENT_DEBUG
    spdlog::info("Socket added: {}", header->fd);
#endif
        epoll_ctl(this->poll_fd, EPOLL_CTL_ADD, header->fd, &event);

        if ((header->socket_info & static_cast<uint8_t>(net::Proto::TCP)) &&
            !(header->socket_info & static_cast<uint8_t>(net::Role::PASSIVE)))
        {
            auto& st = header->state;
            {
                uint64_t s = st.load(std::memory_order_relaxed);
                for (;;)
                {
                    if ((s & usub::utils::sync::refc::COUNT_MASK) == usub::utils::sync::refc::COUNT_MASK) break;
                    uint64_t ns = (s & ~usub::utils::sync::refc::COUNT_MASK) | ((s &
                        usub::utils::sync::refc::COUNT_MASK) + 1);
                    if (st.compare_exchange_weak(s, ns, std::memory_order_acquire, std::memory_order_relaxed)) break;
                    cpu_relax();
                }
            }

            auto* timer = new utils::Timer(settings::timeout_duration_ms, header->fd, utils::TIMEOUT);

            auto coro = utils::timeout_coroutine([this, header]
            {
                header->timeout_epoch_bump(header->state);

                if (header->first) system::this_thread::detail::q->enqueue(header->first);
                if (header->second) system::this_thread::detail::q->enqueue(header->second);

                header->state.fetch_sub(1, std::memory_order_release);
            });

            timer->coro = coro.get_promise()->get_coroutine_handle();
            header->timer_id = this->wheel->addTimer(timer);
        }
        else if ((header->socket_info & static_cast<uint8_t>(net::Proto::TCP)) &&
            (header->socket_info & static_cast<uint8_t>(net::Role::PASSIVE)))
        {
            utils::detail::thread::is_started.store(true, std::memory_order_relaxed);
        }
    }


    void EPoller::updateEvent(net::SocketHeader* header, OperationType initialState)
    {
        struct epoll_event event{};
        event.data.ptr = reinterpret_cast<void*>(header);
        event.events = 0;
        if (header->is_writing_now()) event.events |= (EPOLLOUT | EPOLLET);
        if (header->is_reading_now()) event.events |= (EPOLLIN | EPOLLET);

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

        int result = epoll_ctl(this->poll_fd, EPOLL_CTL_MOD, header->fd, &event);
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

    void EPoller::removeEvent(net::SocketHeader* header, OperationType)
    {
#if UVENT_DEBUG
        spdlog::info("Socket removed: {}", header->fd);
#endif
        using namespace usub::utils::sync::refc;
        uint64_t s = header->state.load(std::memory_order_relaxed);
        for (;;) {
            if (s & CLOSED_MASK) break;
            const uint64_t ns = s | DISCONNECTED_MASK | CLOSED_MASK;
            if (header->state.compare_exchange_weak(s, ns, std::memory_order_release, std::memory_order_relaxed)) break;
            cpu_relax();
        }
        epoll_ctl(this->poll_fd, EPOLL_CTL_DEL, header->fd, nullptr);
        this->wheel->removeTimer(header->timer_id);
        ::close(header->fd);
        header->fd = -1;

        if (header->first)  system::this_thread::detail::q->enqueue(header->first);
        if (header->second) system::this_thread::detail::q->enqueue(header->second);
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
            if (sock->is_busy_now() || sock->is_disconnected_now()) continue;
            if (!(sock->socket_info & static_cast<uint8_t>(net::Proto::TCP) & (sock->socket_info & static_cast<uint8_t>(
                    net::Role::PASSIVE))) &
                (event.events & (EPOLLHUP | EPOLLRDHUP | EPOLLERR)))
            {
                this->removeEvent(sock, ALL);
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
