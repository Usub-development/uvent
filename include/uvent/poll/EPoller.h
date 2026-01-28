//
// Created by kirill on 11/15/24.
//

#ifndef UVENT_EPOLLER_H
#define UVENT_EPOLLER_H

#include <uvent/system/Defines.h>

#include <mutex>
#include <csignal>
#include <utility>
#include "uvent/utils/timer/TimerWheel.h"
#include "PollerBase.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::core
{
    /**
    * \brief Used on Linux systems. Wrapper over epoll.
    */
    class EPoller
    {
    public:
        explicit EPoller(utils::TimerWheel& wheel);

        ~EPoller() = default;

        void addEvent(net::SocketHeader* header, OperationType initialState);

        void updateEvent(net::SocketHeader* header, OperationType initialState);

        void removeEvent(net::SocketHeader* header);

        bool poll(int timeout);

        bool try_lock();

        void unlock();

        void lock_poll(int timeout);

        void deregisterEvent(net::SocketHeader* header) const;

        int get_poll_fd();

    private:
        std::binary_semaphore lock{1};
        int poll_fd{-1};
        uint64_t timeoutDuration_ms{5000};
        std::atomic_bool is_locked{false};

    private:
        /// @brief events returned by epoll
        std::vector<epoll_event> events;
        /// @brief used to ignore signal like: SIGPIPE etc.
        sigset_t sigmask{};
        /// @brief used to store all timers
        utils::TimerWheel& wheel;
    };
}

#endif //UVENT_EPOLLER_H
