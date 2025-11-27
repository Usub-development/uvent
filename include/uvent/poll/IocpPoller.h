//
// IocpPoller.h — Windows backend на WSAPoll (readiness-модель)
//

#ifndef UVENT_IOCPPOLLER_H
#define UVENT_IOCPPOLLER_H

#include <vector>
#include <unordered_map>
#include <atomic>

#include "uvent/system/Defines.h"
#include "uvent/poll/PollerBase.h"
#include "uvent/utils/timer/TimerWheel.h"
#include "uvent/net/SocketMetadata.h"
#include "uvent/net/SocketWindows.h"

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
        utils::TimerWheel* wheel{nullptr};

        std::vector<WSAPOLLFD> pollfds_;
        std::vector<net::SocketHeader*> headers_;

        std::unordered_map<SOCKET, std::size_t> index_by_fd_;

        HANDLE iocp_{nullptr};

        void set_events_for_op(WSAPOLLFD& pfd, OperationType op);
    };

} // namespace usub::uvent::core

#endif // UVENT_IOCPPOLLER_H