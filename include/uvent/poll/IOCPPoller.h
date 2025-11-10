//
// Created by Kirill on 11.11.2025.
//

#ifndef UVENT_IOCPPOLLER_H
#define UVENT_IOCPPOLLER_H

#include "uvent/system/Defines.h"
#include "PollerBase.h"
#include <vector>
#include <mutex>
#include <atomic>
#include <windows.h>

namespace usub::uvent::core {

    enum class IocpOp : uint8_t { Recv0 = 1, WriteKick = 2, Unknown = 255 };

    class IOCPPoller : public PollerBase {
    public:
        explicit IOCPPoller(utils::TimerWheel* wheel);
        ~IOCPPoller() override;

        void addEvent(net::SocketHeader *header, OperationType initialState) override;
        void updateEvent(net::SocketHeader *header, OperationType initialState) override;
        void removeEvent(net::SocketHeader* header, OperationType op) override;

        bool poll(int timeout) override;

        bool try_lock() override;
        void unlock() override;
        void lock_poll(int timeout) override;

    private:
        HANDLE iocp_{nullptr};
        std::vector<OVERLAPPED_ENTRY> entries_;
        utils::TimerWheel* wheel_{nullptr};

        void arm_recv0_(net::SocketHeader* h);
        void kick_write_(net::SocketHeader* h);

        static IocpOp decode_op_(OVERLAPPED* ov) noexcept;
    };

} // namespace usub::uvent::core

#endif //UVENT_IOCPPOLLER_H