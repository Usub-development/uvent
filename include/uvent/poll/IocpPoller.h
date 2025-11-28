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

// СНАЧАЛА winsock2 / ws2tcpip / mswsock
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
// ПОТОМ windows.h, чтобы он НЕ подтащил winsock.h
#include <windows.h>
#endif

#include "uvent/system/Defines.h"
#include "uvent/poll/PollerBase.h"
#include "uvent/utils/timer/TimerWheel.h"
#include "uvent/net/SocketMetadata.h"

namespace usub::uvent::core {

    class IocpPoller : public PollerBase
    {
    public:
        explicit IocpPoller(utils::TimerWheel* wheel);
        ~IocpPoller() override;

        void addEvent(net::SocketHeader* header, OperationType op) override;
        void updateEvent(net::SocketHeader* header, OperationType op) override;
        void removeEvent(net::SocketHeader* header, OperationType op) override;

        bool poll(int timeout_ms) override;

        bool try_lock() override;
        void unlock() override;
        void lock_poll(int timeout_ms) override;

    private:
        HANDLE iocp_handle{nullptr};
        utils::TimerWheel* wheel{nullptr};
        std::vector<OVERLAPPED_ENTRY> events;
    };

} // namespace usub::uvent::core

#endif // UVENT_IOCPPOLLER_H