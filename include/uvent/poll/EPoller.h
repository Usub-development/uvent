//
// Created by kirill on 11/15/24.
//

#ifndef UVENT_EPOLLER_H
#define UVENT_EPOLLER_H

#include <mutex>
#include <csignal>
#include <utility>
#include "uvent/utils/timer/TimerWheel.h"
#include "PollerBase.h"
#include "uvent/tasks/AwaitableFrame.h"

namespace usub::uvent::core {
    /**
    * \brief Used on Linux systems. Wrapper over epoll.
    */
    class EPoller : public PollerBase {
    public:
        explicit EPoller(utils::TimerWheel *wheel);

        ~EPoller() override = default;

        void addEvent(net::SocketHeader *header, OperationType initialState) override;

        void updateEvent(net::SocketHeader *header, OperationType initialState) override;

        void removeEvent(net::SocketHeader* header, OperationType op) override;

        bool poll(int timeout) override;

        bool try_lock() override;

        void unlock() override;

        void lock_poll(int timeout) override;

    private:
        /// @brief events returned by epoll
        std::vector<epoll_event> events;
        /// @brief used to ignore signal like: SIGPIPE etc.
        sigset_t sigmask{};
        /// @brief used to store all timers
        utils::TimerWheel *wheel;
    };
}

#endif //UVENT_EPOLLER_H
