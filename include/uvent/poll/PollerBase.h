//
// Created by kirill on 11/11/24.
//

#ifndef UVENT_POLLERBASE_H
#define UVENT_POLLERBASE_H

#include <memory>
#include <semaphore>
#include "uvent/net/SocketMetadata.h"

namespace usub::uvent::core
{
    enum OperationType
    {
        READ = 1 << 0,
        WRITE = 1 << 1,
        ALL = 3
    };

    enum ActionPolicy
    {
        SINGLE_THREAD,
        MULTI_THREAD
    };

    class PollerBase
    {
    public:
        explicit PollerBase();

        virtual ~PollerBase() = default;

        virtual void addEvent(net::SocketHeader* socket, OperationType initialState) = 0;

        virtual void updateEvent(net::SocketHeader* socket, OperationType initialState) = 0;

        virtual void
        removeEvent(net::SocketHeader* header, OperationType op) = 0;

        virtual bool poll(int timeout) = 0;

        virtual bool try_lock() = 0;

        virtual void unlock() = 0;

        virtual void lock_poll(int timeout) = 0;

        int get_poll_fd();

    public:
        std::binary_semaphore lock{1};

    protected:
        int poll_fd{-1};
        uint64_t timeoutDuration_ms{5000};
        std::atomic_bool is_locked{false};
    };
}

#endif //UVENT_POLLERBASE_H
