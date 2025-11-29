#ifndef UVENT_IOCPPOLLER_H
#define UVENT_IOCPPOLLER_H

#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#endif

#include "uvent/system/Defines.h"
#include "uvent/poll/PollerBase.h"
#include "uvent/utils/timer/TimerWheel.h"
#include "uvent/net/SocketMetadata.h"

namespace usub::uvent::core
{
    class IocpPoller
    {
    public:
        explicit IocpPoller(utils::TimerWheel& wheel);

        ~IocpPoller();

        void addEvent(net::SocketHeader* header, OperationType op);

        void updateEvent(net::SocketHeader* header, OperationType op);

        void removeEvent(net::SocketHeader* header, OperationType op);

        bool poll(int timeout_ms);

        bool try_lock();

        void unlock();

        void lock_poll(int timeout_ms);

    private:
        std::binary_semaphore lock{1};
        std::atomic_bool is_locked{false};

    private:
        HANDLE iocp_handle{nullptr};
        utils::TimerWheel& wheel{nullptr};
        std::vector<OVERLAPPED_ENTRY> events;
    };
} // namespace usub::uvent::core

#endif // UVENT_IOCPPOLLER_H
